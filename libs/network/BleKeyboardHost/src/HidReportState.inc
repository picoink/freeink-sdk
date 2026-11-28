// Backend-independent report state; included inside the anonymous namespace.
uint32_t g_lastReconnectMs = 0;
uint8_t g_reconnectIdx = 0;
// Off once the app calls disconnect(), so a link the user closed stays closed.
// connect() and begin() turn it back on; a link the peer drops leaves it on.
bool g_autoReconnect = true;

// armSelectedPeerReconnect(): while an address is set, auto-reconnect tries only
// that peer, and only while attempts and the window last. Guarded by g_mux.
constexpr uint8_t kSelectedReconnectAttempts = 6;
constexpr uint32_t kSelectedReconnectWindowMs = 120000;
char g_selectedAddr[18] = {0};
uint8_t g_selectedAttemptsLeft = 0;
uint32_t g_selectedStartedMs = 0;

// Caller holds g_mux.
void clearSelectedPlanLocked() {
  g_selectedAddr[0] = '\0';
  g_selectedAttemptsLeft = 0;
}

// HID Report Map hints (parsed once per connection in setupHid). Many BLE
// page-turner remotes are NOT plain boot keyboards: they place their code on the
// Consumer Control page (0x0C) or at a non-standard byte offset. These hints, plus
// the generic-extraction fallback in onReportIngest, let those remotes surface a
// stable key code so the host (capture-then-assign UI) can bind it. g_lastGenericCode
// edge-detects the generic path so one physical press = one key event.
bool g_hasKeyboardPage = false;
bool g_hasConsumerPage = false;
uint8_t g_preferredByteIndex = 0xFF;  // byte the report map suggests holds the code
uint8_t g_lastGenericCode = 0;        // last non-zero code seen on the generic path
volatile uint32_t g_lastReportMs = 0;  // millis() of the last HID notification (stale-release)
// A keyboard whose Report Map has one Input item, a Keyboard-page array of 8- or
// 16-bit slots (Flic Duo: four 16-bit slots, "4F 00 .." for Right Arrow), has no
// modifier byte: its keys start at byte 0. len 0 = no such keyboard.
struct BareKeys {
  uint8_t reportId;
  uint8_t slotBytes;
  uint8_t len;
};
BareKeys g_bareKeys{};

bool isBareKeys(uint8_t reportId, size_t len) {
  return g_bareKeys.len != 0 && reportId == g_bareKeys.reportId && len == g_bareKeys.len;
}

uint8_t g_notifyReportId = 0;  // report id of the characteristic that just notified

// The part of a report raw edges read: its id and up to 8 payload bytes, with the
// modifier byte of a keyboard-shaped report cleared (a held Shift is not a button).
// Same shapes as the key decode: a 9-byte frame leads with its report id, a 7- or
// 8-byte payload with its modifiers.
struct RawFrame {
  uint8_t id;
  uint8_t bytes[8];
};

RawFrame rawFrameOf(const uint8_t* data, size_t len) {
  RawFrame f{};
  size_t offset = 0;
  f.id = g_notifyReportId;
  if (len == 9) {
    f.id = data[0];
    offset = 1;
  }
  const size_t n = len - offset;
  for (size_t i = 0; i < n && i < sizeof(f.bytes); ++i) f.bytes[i] = data[offset + i];
  if (n >= 7 && !isBareKeys(f.id, n)) f.bytes[0] = 0;
  return f;
}

// The button a frame holds: the first byte that differs from `rest`, as
// value | index << 8 | id << 16. 0 = the frame is the rest frame (a release).
uint32_t rawButtonCode(const RawFrame& f, const uint8_t* rest) {
  for (uint32_t i = 0; i < sizeof(f.bytes); ++i) {
    if (f.bytes[i] != rest[i]) return f.bytes[i] | i << 8 | static_cast<uint32_t>(f.id) << 16;
  }
  return 0;
}

// The BareKeys layout of a Report Map, or {}. Walks whole items (HID 1.11 6.2.2),
// so item data is never read as a tag; the hint scan in parseReportMapHints stays
// a byte scan so other remotes keep their behaviour. A second Input, Push/Pop, a
// long item or a map cut mid-item gives {}; so does a report shorter than the
// 7 bytes the boot layout decode starts at.
BareKeys bareKeysOf(const uint8_t* map, size_t len) {
  BareKeys found{};
  uint32_t page = 0, size = 0, count = 0, id = 0;
  for (size_t i = 0; i < len;) {
    const uint8_t prefix = map[i];
    const size_t n = (prefix & 3) == 3 ? 4 : (prefix & 3);
    if (prefix == 0xFE || i + n >= len) return {};
    uint32_t value = 0;
    for (size_t b = 0; b < n; ++b) value |= static_cast<uint32_t>(map[i + 1 + b]) << (8 * b);
    switch (prefix & 0xFC) {
      case 0x04:  // Usage Page
        page = value;
        break;
      case 0x74:  // Report Size
        size = value;
        break;
      case 0x94:  // Report Count
        count = value;
        break;
      case 0x84:  // Report ID
        id = value;
        break;
      case 0xA4:  // Push
      case 0xB4:  // Pop
        return {};
      case 0x80:  // Input: must be the only one, a Data,Array on the Keyboard page
        // count <= 64 keeps the report length in a uint8_t.
        if (found.len || value != 0 || page != 0x07 || (size != 8 && size != 16) || count > 64 ||
            count * size / 8 < 7) {
          return {};
        }
        found = {static_cast<uint8_t>(id), static_cast<uint8_t>(size / 8), static_cast<uint8_t>(count * size / 8)};
        break;
    }
    i += 1 + n;
  }
  return found;
}

// Scan a HID Report Map descriptor for Usage Page (0x05 nn) items and note whether
// a keyboard (0x07) or consumer (0x0C) page is present, plus a heuristic byte index
// where the active code tends to live (keyboard reports: byte[2]; compact consumer
// reports: byte[1]). This is a hint, not a full descriptor parse.
void parseReportMapHints(const uint8_t* map, size_t len) {
  g_hasKeyboardPage = false;
  g_hasConsumerPage = false;
  g_preferredByteIndex = 0xFF;
  if (!map || len < 2) return;
  for (size_t i = 0; i + 1 < len; ++i) {
    if (map[i] == 0x05) {  // Usage Page (1-byte value follows)
      if (map[i + 1] == 0x07) g_hasKeyboardPage = true;
      else if (map[i + 1] == 0x0C) g_hasConsumerPage = true;
    }
  }
  if (g_hasKeyboardPage) g_preferredByteIndex = 2;
  else if (g_hasConsumerPage) g_preferredByteIndex = 1;
}

// Pick a representative "primary" code from a report that the standard keyboard
// slot decode did not handle (consumer / compact / non-standard layouts). Prefers
// the report-map-hinted byte, else the first meaningful non-zero byte. Returns 0
// when the report carries no code (a release frame).
uint8_t extractPrimaryCode(const uint8_t* p, size_t n, size_t* codeIdx = nullptr) {
  const size_t lim = n < 8 ? n : 8;
  // NOTE: do NOT skip 0x01 here. In a keyboard report 0x01 is ErrorRollOver, but in
  // a consumer / vendor report it is a valid button code (e.g. a 3-byte page-turner
  // report of "01 00 00" on press, "00 00 00" on release). Only zero means "no code".
  if (g_preferredByteIndex != 0xFF && g_preferredByteIndex < n && p[g_preferredByteIndex] != 0) {
    if (codeIdx) *codeIdx = g_preferredByteIndex;
    return p[g_preferredByteIndex];
  }
  for (size_t i = 0; i < lim; ++i) {
    if (p[i] != 0) {
      if (codeIdx) *codeIdx = i;
      return p[i];
    }
  }
  return 0;
}

