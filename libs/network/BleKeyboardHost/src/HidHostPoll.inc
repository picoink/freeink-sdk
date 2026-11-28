// Shared stale-input and bounded reconnect work; included inside poll().
  // Held-key release. Page-turner remotes stream a held key (and many omit a clean
  // release frame), so we do NOT synthesize host-side auto-repeat — that turned one
  // tap into dozens of page turns. Instead, when reports stop arriving, age the held
  // key / last generic code out so one physical press == one event and the next press
  // (even of the same button) re-triggers. Covers both the keyboard and generic paths.
  portENTER_CRITICAL(&g_mux);
  const uint8_t held = heldUsage_;
  portEXIT_CRITICAL(&g_mux);
  if ((held != 0 || g_lastGenericCode != 0) && (millis() - g_lastReportMs) > kReleaseTimeoutMs) {
    portENTER_CRITICAL(&g_mux);
    heldUsage_ = 0;
    portEXIT_CRITICAL(&g_mux);
    memset(prevKeys_, 0, sizeof(prevKeys_));
    g_lastGenericCode = 0;
  }

  // Silence ends a raw button only on a remote that was streaming it; one that
  // sends a frame per edge is still holding and sends its own release. The
  // release is dated by the last frame, which is when the button came up.
  portENTER_CRITICAL(&g_mux);
  if (rawCode_ != 0 && rawReports_ > 1 && (millis() - g_lastReportMs) > kReleaseTimeoutMs) {
    if (!rawDropped_) pushRawLocked(rawCode_, false, g_lastReportMs, 0, 0, 0);
    rawDropped_ = false;
    rawCode_ = 0;
    rawReports_ = 0;
  }
  portEXIT_CRITICAL(&g_mux);

  // Auto-reconnect to a bonded HID peripheral: the selected one while a plan is
  // armed (never another bond), otherwise each bond in turn.
  char selected[sizeof(g_selectedAddr)] = {0};
  portENTER_CRITICAL(&g_mux);
  const bool planArmed = g_selectedAddr[0] != '\0';
  if (planArmed && g_selectedAttemptsLeft > 0 && millis() - g_selectedStartedMs < kSelectedReconnectWindowMs &&
      millis() - g_lastReconnectMs >= kReconnectBackoffMs && !connected_ && !g_connecting && !scanning_) {
    memcpy(selected, g_selectedAddr, sizeof(selected));
    --g_selectedAttemptsLeft;
  }
  portEXIT_CRITICAL(&g_mux);
  if (selected[0]) {
    connectInternal(selected, /*explicitRequest=*/false);
  } else if (!planArmed && g_autoReconnect && !connected_ && !g_connecting && !scanning_ && bondCount_ > 0) {
    const uint32_t now = millis();
    if (now - g_lastReconnectMs > kReconnectBackoffMs) {
      g_lastReconnectMs = now;
      g_reconnectIdx = static_cast<uint8_t>(g_reconnectIdx % bondCount_);
      connect(bonds_[g_reconnectIdx].addr);
      g_reconnectIdx++;
    }
  }
