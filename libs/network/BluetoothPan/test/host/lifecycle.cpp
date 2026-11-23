// Compile the real worker and callbacks against deterministic radio/RTOS fakes.
#include "../../src/BluetoothPan.cpp"
using State = Pan::State;
static const uint8_t phone[6]{2, 3, 4, 5, 6, 7};
static void openPhone() {
  esp_pan_cb_param_t p{};
  p.open.handle = 7;
  memcpy(p.open.remote_bda, phone, 6);
  panCallback(ESP_PAN_OPEN_EVT, &p);
}
static void pairPhone() {
  esp_bt_gap_cb_param_t p{};
  memcpy(p.cfm_req.bda, phone, 6);
  p.cfm_req.num_val = 123456;
  gapCallback(ESP_BT_GAP_CFM_REQ_EVT, &p);
}
static void gotIp() {
  ip_event_got_ip_t p{context.netif, {{172, 20, 10, 2}}};
  ipEvent(nullptr, IP_EVENT, IP_EVENT_ETH_GOT_IP, &p);
}
static void run(const std::function<void()>& events) {
  fake::ticks = 0;
  fake::onWait = events;
  assert(Pan::instance().begin() == Pan::Result::Ok);
  assert(Pan::instance()
             .status()
             .enabled);  // No start/end race before the task is scheduled.
  worker(nullptr);
  fake::onWait = {};
  assert(!context.running && !context.accepting && !fake::profile &&
         !fake::bluedroid);
  assert(!fake::nets && !fake::opens && context.packets->data.empty());
  assert(!Pan::instance().status().enabled);
}
int main() {
  auto& pan = Pan::instance();
  assert(pan.supported());
  assert(!fake::starts &&
         !fake::opens);  // Capability/status do not initialize radio or NVS.
  assert(!pan.status().enabled);
  fake::taskOk = false;
  assert(pan.begin() == Pan::Result::Failed && !pan.status().enabled);
  fake::taskOk = true;
  assert(pan.begin() == Pan::Result::Ok);
  for (int i = 0; i < 100; ++i) assert(pan.end() == Pan::Result::Ok);
  fake::onWait = [] { assert(false); };
  worker(nullptr);
  fake::onWait = {};
  assert(pan.status().state == State::Off && !fake::nets && !fake::opens);
  unsigned step = 0;
  run([&] {
    auto s = pan.status();
    switch (step++) {
      case 0:
        assert(s.state == State::Idle);
        assert(pan.connect(phone) == Pan::Result::Ok);
        break;
      case 1:
        assert(s.state == State::Connecting);
        pairPhone();
        break;
      case 2:
        assert(s.pairing && s.pairingCode == 123456 && !fake::confirms);
        assert(pan.confirmPairing(true) == Pan::Result::Ok);
        break;
      case 3:
        assert(s.state == State::Connecting);
        openPhone();
        break;
      case 4:
        assert(s.state == State::ObtainingIp && !s.saved);
        gotIp();
        break;
      case 5: {
        assert(s.state == State::Connected && s.saved && s.connection == 1);
        assert(!strcmp(s.ip, "172.20.10.2") && fake::confirms == 1);
        uint8_t payload[1501]{};
        esp_pan_cb_param_t p{};
        p.data_ind.handle = 7;
        p.data_ind.data = payload;
        p.data_ind.protocol = 0x0800;
        p.data_ind.len = 1501;
        panCallback(ESP_PAN_DATA_IND_EVT, &p);
        assert(context.packets->data.empty());
        p.data_ind.len = 1500;
        for (int i = 0; i < 9; ++i) panCallback(ESP_PAN_DATA_IND_EVT, &p);
        assert(context.packets->data.size() == 8 &&
               pan.status().droppedFrames == 1);
        uint8_t frame[1514]{};
        frame[12] = 8;
        assert(transmit(nullptr, frame, sizeof(frame)) == ESP_OK);
        assert(fake::transmitted.size() == 1500);
        context.congested = true;
        assert(transmit(nullptr, frame, sizeof(frame)) ==
               ESP_ERR_INVALID_STATE);
        context.congested = false;
        break;
      }
      case 6:
        assert(fake::packets == 8 && s.state == State::Connected);
        assert(fake::received.size() == 1514 && fake::received[12] == 8);
        esp_pan_disconnect(7);
        break;
      case 7:
        assert(s.state == State::RetryWait && !s.ip[0]);
        fake::now += 2000;
        break;
      case 8:
        assert(s.state == State::Connecting);
        openPhone();
        break;
      case 9:
        gotIp();
        break;
      case 10:
        assert(s.state == State::Connected && s.connection == 2);
        pan.forget();
        break;
      default:
        assert(false);
    }
  });
  assert(fake::saved.empty() && fake::bonds && !pan.status().saved);
  // Rejecting pairing drains the profile before another connection can start.
  step = 0;
  run([&] {
    switch (step++) {
      case 0:
        pan.connect(phone);
        break;
      case 1:
        pairPhone();
        break;
      case 2:
        pan.confirmPairing(false);
        break;
      default:
        assert(false);
    }
  });
  assert(pan.status().state == State::Error && fake::rejects);
  // Cancel while connecting, then a late OPEN: never accept it or persist it.
  step = 0;
  const auto saves = fake::savedWrites;
  run([&] {
    if (step++ == 0)
      pan.connect(phone);
    else {
      pan.disconnect();
      openPhone();
    }
  });
  assert(fake::savedWrites == saves && !pan.status().enabled);
  // Repeated enable/stop cycles release all runtime resources.
  for (int i = 0; i < 20; ++i) run([&] { pan.end(); });
  assert(fake::starts == fake::stops);
}
