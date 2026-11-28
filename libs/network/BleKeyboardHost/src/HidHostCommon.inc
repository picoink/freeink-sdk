// Backend-independent input and persistence methods; included inside namespace freeink.
// --- Translated input --------------------------------------------------------
bool BleKeyboardHost::popKey(KeyEvent& out) {
  bool got = false;
  portENTER_CRITICAL(&g_mux);
  if (ringHead_ != ringTail_) {
    out = ring_[ringTail_];
    ringTail_ = static_cast<uint8_t>((ringTail_ + 1) % kKeyQueueLen);
    got = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return got;
}

bool BleKeyboardHost::popRawButton(RawButtonEvent& out) {
  bool got = false;
  portENTER_CRITICAL(&g_mux);
  if (rawHead_ != rawTail_) {
    out = rawRing_[rawTail_];
    rawTail_ = static_cast<uint8_t>((rawTail_ + 1) % kRawQueueLen);
    got = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return got;
}

bool BleKeyboardHost::pushRawLocked(const uint32_t code, const bool pressed, const uint32_t atMs,
                                    const uint8_t keycode, const uint8_t mods, const uint8_t keep,
                                    const bool wasRest) {
  const uint8_t used = static_cast<uint8_t>((rawHead_ - rawTail_ + kRawQueueLen) % kRawQueueLen);
  if (kRawQueueLen - 1 - used <= keep) return false;  // drop rather than block, like the key ring
  RawButtonEvent& e = rawRing_[rawHead_];
  e.value = static_cast<uint8_t>(code);
  e.byteIndex = static_cast<uint8_t>(code >> 8);
  e.reportId = static_cast<uint8_t>(code >> 16);
  e.pressed = pressed;
  e.keycode = keycode;
  e.mods = mods;
  e.wasRest = wasRest;
  e.atMs = atMs;
  rawHead_ = static_cast<uint8_t>((rawHead_ + 1) % kRawQueueLen);
  return true;
}

void BleKeyboardHost::enqueue(const KeyEvent& ev) {
  portENTER_CRITICAL(&g_mux);
  const uint8_t next = static_cast<uint8_t>((ringHead_ + 1) % kKeyQueueLen);
  if (next != ringTail_) {  // drop on overflow rather than block
    ring_[ringHead_] = ev;
    ringHead_ = next;
  }
  portEXIT_CRITICAL(&g_mux);
}

void BleKeyboardHost::emitUsage(uint8_t usage, uint8_t mods) {
  if (usage == 0) return;
  char ch;
  SpecialKey special;
  // Best-effort translation: known keyboard usages get a char / SpecialKey. Unknown
  // codes (page-turner consumer codes, vendor layouts) still surface with keycode set
  // so a capture-then-assign UI can bind them — hidTranslate already zeroes ch/special.
  hidTranslate(usage, mods, ch, special);
  KeyEvent ev;
  ev.ch = ch;
  ev.keycode = usage;
  ev.mods = mods;
  ev.special = special;
  ev.pressed = true;
  if (framePressUsage_ == 0) {  // the key a raw press of this frame carries
    framePressUsage_ = usage;
    framePressMods_ = mods;
  }
  enqueue(ev);
}

// --- Internal hooks from the BLE backend -------------------------------------
void BleKeyboardHost::onReportIngest(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  const uint32_t now = millis();
  const uint32_t lastMs = g_lastReportMs;
  g_lastReportMs = now;  // freshness for the stale-release timeout in poll()
  framePressUsage_ = 0;
  framePressMods_ = 0;
  frameAxisPad_ = false;

#if FREEINK_BLE_HID_REPORT_DEBUG
  {
    char buf[64];
    size_t off = 0;
    const size_t dump = len < 16 ? len : 16;
    for (size_t i = 0; i < dump && off + 3 < sizeof(buf); ++i) {
      off += snprintf(buf + off, sizeof(buf) - off, "%02X ", data[i]);
    }
    Serial.printf("[BleHid] report len=%u %s\n", (unsigned)len, buf);
  }
#endif

  // Normalize: strip a leading report id (len 9). Boot/report-protocol keyboard
  // reports are [mod][reserved][k0..k5] (8 bytes) or a compact [mod][k0..k5] (7).
  const uint8_t* p = data;
  size_t n = len;
  if (n == 9) {
    p += 1;
    n -= 1;
  }

  // --- Standard keyboard report path -----------------------------------------
  uint8_t mod = 0;
  uint8_t keys[6] = {0};
  bool keyboardShaped = false;
  if (isBareKeys(g_notifyReportId, n)) {
    // Every slot from byte 0; the first six keys are kept. Keyboard usages end at
    // 0xFF, so a 16-bit slot whose high byte is set holds no key.
    const size_t w = g_bareKeys.slotBytes;
    size_t k = 0;
    for (size_t i = 0; i + w <= n && k < 6; i += w) {
      if (p[i] && (w == 1 || !p[i + 1])) keys[k++] = p[i];
    }
    keyboardShaped = true;
  } else if (n >= 8) {
    mod = p[0];
    for (int i = 0; i < 6; ++i) keys[i] = p[2 + i];
    keyboardShaped = true;
  } else if (n == 7) {
    mod = p[0];
    for (int i = 0; i < 6; ++i) keys[i] = p[1 + i];
    keyboardShaped = true;
  }

  bool emittedKb = false;
  bool keyHeld = false;  // the key slots still hold a key from an earlier report
  if (keyboardShaped) {
    // Emit a press for every key newly present versus the previous report.
    for (int i = 0; i < 6; ++i) {
      const uint8_t k = keys[i];
      if (k == 0 || k == 0x01 /*ErrorRollOver*/) continue;
      bool wasDown = false;
      for (int j = 0; j < 6; ++j) {
        if (prevKeys_[j] == k) {
          wasDown = true;
          break;
        }
      }
      if (!wasDown) {
        emitUsage(k, mod);
        emittedKb = true;
      }
    }

    // Track the last held key for auto-repeat.
    uint8_t cur = 0;
    for (int i = 0; i < 6; ++i) {
      if (keys[i] != 0 && keys[i] != 0x01) cur = keys[i];
    }
    keyHeld = cur != 0;
    portENTER_CRITICAL(&g_mux);
    if (cur == 0) {
      heldUsage_ = 0;
    } else if (cur != heldUsage_) {
      heldUsage_ = cur;
      heldMods_ = mod;
      heldSince_ = millis();
      lastRepeat_ = millis();
    }
    portEXIT_CRITICAL(&g_mux);

    memcpy(prevKeys_, keys, sizeof(prevKeys_));
  }

  // --- Generic fallback for non-keyboard remotes -----------------------------
  // Many page turners are not boot keyboards: they emit on the Consumer Control
  // page or place the code at a non-standard byte. When the keyboard slots produced
  // nothing and the device doesn't look like a pure keyboard, scan the report for a
  // representative code and surface it (edge-detected so one press == one event).
  // A report whose key slots still hold a key belongs to the keyboard path: a
  // remote that streams a held key repeats it, and each repeat would read here
  // as a new press.
  const bool tryGeneric = !emittedKb && !keyHeld && (g_hasConsumerPage || !g_hasKeyboardPage || n < 7);
  if (tryGeneric) {
    size_t codeIdx = 0;
    const uint8_t code = extractPrimaryCode(p, n, &codeIdx);
    // Gamepad-style modes keep constant bits in the button byte and only clear
    // the pressed bit on release (ino gamebrick mode T: 0x13 pressed -> 0x12
    // released, bit0 is the button). "code changed" alone emits a phantom
    // second press on that release frame. A press must SET at least one bit
    // the previous code didn't have; a bit-subset of the previous code is a
    // (partial) release, never a new press. Value-coded remotes are unaffected
    // in practice: their release frame is all-zero (code 0), and the 150 ms
    // stale-release timeout in poll() zeroes g_lastGenericCode between any two
    // human-separated presses.
    const bool pressEdge = code != 0 && code != g_lastGenericCode && (code & ~g_lastGenericCode) != 0;
    const bool releaseEdge = code != 0 && code != g_lastGenericCode && (code & ~g_lastGenericCode) == 0;
    if (codeIdx == 0 && n >= 5) {
      frameAxisPad_ = true;
      // Gamepad/axis-pair mode (ino gamebrick T): byte 0 is a status byte whose
      // bit0 is "pressed" (0x13 press -> 0x12 release), bytes 1-4 are two 16-bit
      // LE axes centered at ~2000. Every button raises the same pressed bit; the
      // identity lives in where the axes sit. The FIRST press frame is not
      // trustworthy (a direction press starts near center and ramps toward its
      // extreme over several frames), but the release frame still carries the
      // final held values ("12 D0 07 84 03" = up, "12 D0 07 10 0E" = down,
      // "12 B8 0B D0 07" = A, "12 B8 0B C4 09" = B). So classify on the release
      // edge: quantize each axis into low/center/high and emit a synthetic code
      // from the zone pair. Both-axes-centered carries no identity (bare
      // release) and emits nothing.
      if (releaseEdge) {
        uint16_t axis1, axis2;  // memcpy: p may be unaligned (RISC-V faults on unaligned loads)
        memcpy(&axis1, p + 1, sizeof(axis1));
        memcpy(&axis2, p + 3, sizeof(axis2));
        constexpr uint16_t kAxisCenter = 2000;
        constexpr uint16_t kAxisDeadband = 300;  // B rests at center+500; center noise stays inside +/-300
        const auto zone = [](uint16_t v) -> uint8_t {
          if (v < kAxisCenter - kAxisDeadband) return 0;
          if (v > kAxisCenter + kAxisDeadband) return 2;
          return 1;
        };
        const uint8_t zonePair = zone(axis1) * 3 + zone(axis2);
        if (zonePair != 4) {  // 4 = both centered: nothing to bind
          emitUsage(0x40 + zonePair, 0);
        }
      }
    } else if (pressEdge) {
      // Value-coded remotes (e.g. "01 00 00" press, all-zero release): the code
      // byte IS the identity; emit on the press edge as before.
      emitUsage(code, 0);
    }
    g_lastGenericCode = code;
  } else if (emittedKb) {
    g_lastGenericCode = 0;
  }
  ingestRawEdge(data, len, now, lastMs);
}

void BleKeyboardHost::ingestRawEdge(const uint8_t* data, size_t len, const uint32_t now, const uint32_t lastMs) {
  // Each report id is read against its rest frame. A frame that only clears bits
  // of the one before is a release and becomes the rest; when the first frame of
  // a report is followed by one that only adds bits, that first frame was the rest.
  const RawFrame f = rawFrameOf(data, len);
  uint8_t slot = 0;
  while (slot < rawRestCount_ && rawRest_[slot].id != f.id) ++slot;
  if (slot == rawRestCount_) {
    if (rawRestCount_ < 4) ++rawRestCount_;
    slot = rawRestCount_ - 1;
    rawRest_[slot] = RawRest{};
    rawRest_[slot].id = f.id;
  }
  RawRest& r = rawRest_[slot];
  uint8_t adds = 0;
  uint8_t clears = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    adds |= f.bytes[i] & ~r.prev[i];
    clears |= r.prev[i] & ~f.bytes[i];
  }
  if (clears && !adds) {
    memcpy(r.rest, f.bytes, sizeof(r.rest));
    r.known = true;
  }
  if (adds && !clears && r.frames == 1) {
    memcpy(r.rest, r.prev, sizeof(r.rest));
    r.known = true;
  }
  memcpy(r.prev, f.bytes, sizeof(r.prev));
  if (r.frames < 2) ++r.frames;
  // An axis-pair gamepad names its button only through the zone the decoder reads;
  // its axis bytes ramp while held and would read as a new button on every frame.
  const uint32_t code = frameAxisPad_ ? 0 : rawButtonCode(f, r.rest);

  portENTER_CRITICAL(&g_mux);
  bool pressTried = false;
  if (code != rawCode_) {
    // Release before press, so a swap of one button for another reads in that
    // order. A button held until now that was read against the zero guess, whose
    // byte the rest learned since carries, was the rest frame, not a button.
    bool heldWasRest = false;
    for (uint8_t i = 0; rawCode_ != 0 && rawGuessed_ && i < rawRestCount_; ++i) {
      if (rawRest_[i].id != static_cast<uint8_t>(rawCode_ >> 16)) continue;
      const uint8_t at = static_cast<uint8_t>(rawCode_ >> 8);
      heldWasRest = at < 8 && rawRest_[i].known && rawRest_[i].rest[at] == static_cast<uint8_t>(rawCode_);
      break;
    }
    if (rawCode_ != 0 && !rawDropped_) pushRawLocked(rawCode_, false, now, 0, 0, 0, heldWasRest);
    pressTried = code != 0;
    rawDropped_ = pressTried && !pushRawLocked(code, true, now, framePressUsage_, framePressMods_, 1);
    rawCode_ = code;
    rawGuessed_ = !r.known;
    rawReports_ = 1;
  } else if (code != 0) {
    if (now - lastMs > kReleaseTimeoutMs) {
      // The same frame after silence, with no release frame between: a remote that
      // only reports presses. Each such frame is a new press; the one before it is
      // settled first so every press keeps exactly one release.
      if (!rawDropped_) pushRawLocked(code, false, lastMs, 0, 0, 0);
      pressTried = true;
      rawDropped_ = !pushRawLocked(code, true, now, framePressUsage_, framePressMods_, 1);
      rawReports_ = 1;
    } else if (rawReports_ < 255) {
      ++rawReports_;
    }
  }
  if (framePressUsage_ != 0 && !pressTried) {
    // The decoder read a key that no byte edge carries (the zone of an axis gamepad,
    // a key added while another is held). It gets an identity of its own, byte index
    // 0xFF (never a payload byte), as one tap.
    const uint8_t open = rawCode_ != 0 && !rawDropped_ ? 1 : 0;
    const uint32_t k = 0xFFFF00u | framePressUsage_;
    if (pushRawLocked(k, true, now, framePressUsage_, framePressMods_, static_cast<uint8_t>(1 + open))) {
      pushRawLocked(k, false, now, 0, 0, open);
    }
  }
  portEXIT_CRITICAL(&g_mux);
}

void BleKeyboardHost::onScanResultIngest(const char* addr, const char* name, int rssi, uint8_t type, bool hid,
                                         bool connectable) {
  if (!addr) return;
  // A "real" name (not the address fallback) should never be downgraded back to
  // the address on a later primary-only advertisement.
  const bool realName = name && name[0] && strcmp(name, addr) != 0;
#if !FREEINK_BLE_HID_SHOW_UNNAMED_DEVICES
  if (!realName && !hid) {
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] scan filtered: %s name='%s' rssi=%d hid=%d conn=%d type=%u\n", addr, name ? name : "",
                  rssi, hid ? 1 : 0, connectable ? 1 : 0, type);
#endif
    return;
  }
#endif
  portENTER_CRITICAL(&g_mux);

  // Upsert by address.
  uint8_t idx = deviceCount_;
  for (uint8_t i = 0; i < deviceCount_; ++i) {
    if (strncmp(devices_[i].addr, addr, sizeof(devices_[i].addr)) == 0) {
      idx = i;
      break;
    }
  }

  if (idx != deviceCount_) {
    // Known device: refresh RSSI; upgrade to a real name / HID flag if we just
    // learned one (these can arrive in a later advertisement or scan response).
    DiscoveredDevice& d = devices_[idx];
    d.rssi = rssi;
    if (hid) d.hid = true;
    if (connectable) d.connectable = true;
    if (realName) {
      strncpy(d.name, name, sizeof(d.name) - 1);
      d.name[sizeof(d.name) - 1] = '\0';
      d.hasName = true;
    }
    portEXIT_CRITICAL(&g_mux);
    return;
  }

  // New device.
  if (deviceCount_ < kMaxDiscovered) {
    idx = deviceCount_++;
  } else {
    // Full: evict the weakest-RSSI entry if this one is stronger, so nearby
    // input peripherals are not crowded out by distant beacons.
    uint8_t weakest = 0;
    for (uint8_t i = 1; i < deviceCount_; ++i) {
      if (devices_[i].rssi < devices_[weakest].rssi) weakest = i;
    }
    if (rssi <= devices_[weakest].rssi) {
      portEXIT_CRITICAL(&g_mux);
      return;
    }
    idx = weakest;
  }

  DiscoveredDevice& d = devices_[idx];
  strncpy(d.addr, addr, sizeof(d.addr) - 1);
  d.addr[sizeof(d.addr) - 1] = '\0';
  strncpy(d.name, name && name[0] ? name : addr, sizeof(d.name) - 1);
  d.name[sizeof(d.name) - 1] = '\0';
  d.rssi = rssi;
  d.addrType = type;
  d.hasName = realName;
  d.hid = hid;
  d.connectable = connectable;
#if FREEINK_BLE_HID_SCAN_DEBUG
  Serial.printf("[BleHid] scan accepted: idx=%u count=%u addr=%s name='%s' rssi=%d hid=%d conn=%d type=%u\n", idx,
                deviceCount_, d.addr, d.name, d.rssi, d.hid ? 1 : 0, d.connectable ? 1 : 0, d.addrType);
#endif
  portEXIT_CRITICAL(&g_mux);
}

void BleKeyboardHost::onLinkUp(const char* addr, const char* name, uint8_t type) {
  // Resolve a friendly name from the scan or bond lists if the caller has none.
  const char* resolved = (name && name[0] && (!addr || strcmp(name, addr) != 0)) ? name : nullptr;
  if (!resolved && addr) {
    for (uint8_t i = 0; i < deviceCount_; ++i) {
      if (strncmp(devices_[i].addr, addr, sizeof(devices_[i].addr)) == 0 && devices_[i].hasName &&
          devices_[i].name[0] && strcmp(devices_[i].name, addr) != 0) {
        resolved = devices_[i].name;
        break;
      }
    }
  }
  if (!resolved && addr) {
    for (uint8_t i = 0; i < bondCount_; ++i) {
      if (strncmp(bonds_[i].addr, addr, sizeof(bonds_[i].addr)) == 0 && bonds_[i].name[0]) {
        resolved = bonds_[i].name;
        break;
      }
    }
  }
  connAddr_[0] = '\0';
  if (addr) {
    strncpy(connAddr_, addr, sizeof(connAddr_) - 1);
    connAddr_[sizeof(connAddr_) - 1] = '\0';
  }
  connName_[0] = '\0';
  if (resolved) {
    strncpy(connName_, resolved, sizeof(connName_) - 1);
    connName_[sizeof(connName_) - 1] = '\0';
  } else if (addr) {
    strncpy(connName_, addr, sizeof(connName_) - 1);
    connName_[sizeof(connName_) - 1] = '\0';
  }

  // Persist the pairing if new.
  if (addr) {
    bool known = false;
    for (uint8_t i = 0; i < bondCount_; ++i) {
      if (strncmp(bonds_[i].addr, addr, sizeof(bonds_[i].addr)) == 0) {
        if (resolved && strcmp(bonds_[i].name, resolved) != 0) {
          strncpy(bonds_[i].name, resolved, sizeof(bonds_[i].name) - 1);
          bonds_[i].name[sizeof(bonds_[i].name) - 1] = '\0';
          bonds_[i].addrType = type;
          persistBonds();
        }
        known = true;
        break;
      }
    }
    if (!known && bondCount_ < kMaxBonds) {
      PairedHidDevice& b = bonds_[bondCount_++];
      strncpy(b.addr, addr, sizeof(b.addr) - 1);
      b.addr[sizeof(b.addr) - 1] = '\0';
      b.name[0] = '\0';
      if (resolved) {
        strncpy(b.name, resolved, sizeof(b.name) - 1);
        b.name[sizeof(b.name) - 1] = '\0';
      }
      b.addrType = type;
      persistBonds();
    }
  }

  g_reconnectIdx = 0;
  connecting_ = false;
  connected_ = true;
}

void BleKeyboardHost::onLinkDown() {
  portENTER_CRITICAL(&g_mux);
  // The selected peer's link dropped on its own (disconnect() clears the plan
  // first): a fresh bounded plan brings it back.
  if (connected_ && g_selectedAddr[0] && strncmp(g_selectedAddr, connAddr_, sizeof(g_selectedAddr)) == 0) {
    g_selectedAttemptsLeft = kSelectedReconnectAttempts;
    g_selectedStartedMs = millis();
    g_lastReconnectMs = g_selectedStartedMs;
  }
  connected_ = false;
  connecting_ = false;
  connAddr_[0] = '\0';
  heldUsage_ = 0;
  // No button is held across a link drop, and the next remote may rest on other bytes.
  rawCode_ = 0;
  rawReports_ = 0;
  rawDropped_ = false;
  portEXIT_CRITICAL(&g_mux);
  rawRestCount_ = 0;
  // A link that drops while a key is down never delivers its release. Forget the
  // key, so its first press on the next link reads as a press.
  memset(prevKeys_, 0, sizeof(prevKeys_));
  g_lastGenericCode = 0;
}

void BleKeyboardHost::onConnectFailed(const char* reason) {
  connected_ = false;
  connecting_ = false;
  portENTER_CRITICAL(&g_mux);
  strncpy(connectFailure_, reason && reason[0] ? reason : "Connection failed", sizeof(connectFailure_) - 1);
  connectFailure_[sizeof(connectFailure_) - 1] = '\0';
  connectFailed_ = true;
  heldUsage_ = 0;
  if (g_selectedAddr[0]) g_lastReconnectMs = millis();  // the next attempt waits from the failure
  portEXIT_CRITICAL(&g_mux);
}

bool BleKeyboardHost::takeConnectFailure(char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  bool failed = false;
  portENTER_CRITICAL(&g_mux);
  if (connectFailed_) {
    strncpy(out, connectFailure_, outLen - 1);
    out[outLen - 1] = '\0';
    connectFailed_ = false;
    failed = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return failed;
}

void BleKeyboardHost::onPairingPasskey(uint32_t passkey) {
  portENTER_CRITICAL(&g_mux);
  pairingPasskey_ = passkey;
  pairingPasskeyReady_ = true;
  portEXIT_CRITICAL(&g_mux);
}

bool BleKeyboardHost::takePairingPasskey(uint32_t& out) {
  bool ready = false;
  portENTER_CRITICAL(&g_mux);
  if (pairingPasskeyReady_) {
    out = pairingPasskey_;
    pairingPasskeyReady_ = false;
    ready = true;
  }
  portEXIT_CRITICAL(&g_mux);
  return ready;
}

// --- NVS persistence ---------------------------------------------------------
void BleKeyboardHost::loadBonds() {
  Preferences p;
  if (!p.begin("freeink-hid", true)) return;
  bondCount_ = p.getUChar("n", 0);
  if (bondCount_ > kMaxBonds) bondCount_ = kMaxBonds;
  if (bondCount_ > 0) p.getBytes("b", bonds_, bondCount_ * sizeof(PairedHidDevice));
  p.end();
  bool cleaned = false;
  for (uint8_t i = 0; i < bondCount_; ++i) {
    if (bonds_[i].name[0] && strcmp(bonds_[i].name, bonds_[i].addr) == 0) {
      bonds_[i].name[0] = '\0';
      cleaned = true;
    }
  }
  if (cleaned) persistBonds();
}

void BleKeyboardHost::persistBonds() {
  Preferences p;
  if (!p.begin("freeink-hid", false)) return;
  p.putUChar("n", bondCount_);
  if (bondCount_ > 0) p.putBytes("b", bonds_, bondCount_ * sizeof(PairedHidDevice));
  p.end();
}



bool BleKeyboardHost::connectOnly(const char* addr) {
  if (!addr || !addr[0]) return false;
  portENTER_CRITICAL(&g_mux);
  g_autoReconnect = false;
  clearSelectedPlanLocked();
  portEXIT_CRITICAL(&g_mux);
  if (!connectInternal(addr, false)) return false;
  portENTER_CRITICAL(&g_mux);
  snprintf(g_selectedAddr, sizeof(g_selectedAddr), "%s", addr);
  g_selectedAttemptsLeft = 0; // A later link loss re-arms only this address.
  portEXIT_CRITICAL(&g_mux);
  return true;
}
