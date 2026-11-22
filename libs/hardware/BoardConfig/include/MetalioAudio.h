#pragma once

// Shared by AudioManager and Microphone. Only include in Metalio audio paths.
// Lifecycle calls belong to one hardware/control task. One reader and one
// writer may stream concurrently; end/powerDown must not race their own I/O.
#include <BoardConfig.h>
#include <HardwareSerial.h>
#include <driver/i2s_std.h>
#include <detail/MetalioPcm.h>

namespace freeink { namespace metalio {
constexpr uint32_t AUDIO_RATE = 16000;
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
inline bool ensureBooted() { return metalio_plus::ensureBooted(); }
inline bool setAmplifier(bool enabled) { return metalio_plus::setAmplifier(enabled); }
#endif

struct AudioBus {
  i2s_chan_handle_t tx = nullptr;
  i2s_chan_handle_t rx = nullptr;
  const void* speaker = nullptr;
  const void* microphone = nullptr;
  bool txEnabled = false;
  bool rxEnabled = false;
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  HardwareSerial uart{1};
#else
  HardwareSerial uart{2};
#endif
};

inline AudioBus& audioBus() {
  static AudioBus bus;
  return bus;
}

inline void destroyAudioBus() {
  auto& b = audioBus();
  if (b.txEnabled) i2s_channel_disable(b.tx);
  if (b.rxEnabled) i2s_channel_disable(b.rx);
  b.txEnabled = b.rxEnabled = false;
  if (b.tx) i2s_del_channel(b.tx);
  if (b.rx) i2s_del_channel(b.rx);
  b.tx = b.rx = nullptr;
  b.uart.end();
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  metalio_plus::setAmplifier(false);
  // P1.4 also supplies the panel PMIC on Plus hardware. Only board sleep
  // may cut this rail after the display has powered down.
#endif
}

#if FREEINK_DEVICE_METALIO_EINK4_PLUS
inline bool confirmModuleMode(HardwareSerial& uart) {
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    while (uart.available()) uart.read();
    if (uart.print("AT+MODE=1\r\n") != 11) return false;
    uart.flush();
    char line[128] = {};
    size_t used = 0;
    bool overflow = false;
    const auto start = millis();
    while (millis() - start < 1000) {
      while (uart.available()) {
        const char c = uart.read();
        if (c == '\r' || c == '\n') {
          line[used] = 0;
          const char* mode = strstr(line, "SET MODE 1");
          if (!overflow && mode && (mode[10] == 0 || mode[10] == ' ') && !strstr(line, "ERROR") && !strstr(line, "FAIL")) return true;
          if (strstr(line, "ERROR") || strstr(line, "FAIL")) break;
          used = 0; overflow = false;
        } else if (used < sizeof(line) - 1) line[used++] = c;
        else overflow = true;
      }
      delay(5);
    }
    if (attempt < 3) delay(1000);
  }
  return false;
}
#endif

inline bool acquireAudio(const void* owner, bool input) {
  auto& b = audioBus();
  const void*& slot = input ? b.microphone : b.speaker;
  if (slot) return slot == owner;
  if (!b.tx) {
    if (!ensureBooted() || !setAmplifier(false)) return false;
    // Vendor InitializeBTAudio/BtDefaultModeTask: UART2, mode 1 supplies clocks.
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
    if (!metalio_plus::setOutput(metalio_plus::PIN_BT_AMP_POWER, true)) return false;
    delay(700);
    b.uart.begin(115200, SERIAL_8N1, 38, 39);
#else
    b.uart.begin(115200, SERIAL_8N1, 47, 48);
#endif
    if (b.uart.print("AT+RX=2\r\n") != 9) { destroyAudioBus(); return false; }
    b.uart.flush();
    delay(700);
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
    if (!confirmModuleMode(b.uart)) { destroyAudioBus(); return false; }
#else
    if (b.uart.print("AT+MODE=1\r\n") != 11) { b.uart.end(); return false; }
    b.uart.flush();
#endif

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);
    chan.auto_clear = true;
    if (i2s_new_channel(&chan, &b.tx, &b.rx) != ESP_OK) {
      destroyAudioBus();
      return false;
    }
    i2s_std_config_t cfg = {};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE);
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(BoardConfig::ACTIVE.audio.bclk);
    cfg.gpio_cfg.ws = static_cast<gpio_num_t>(BoardConfig::ACTIVE.audio.lrclk);
    cfg.gpio_cfg.dout = static_cast<gpio_num_t>(BoardConfig::ACTIVE.audio.dout);
    cfg.gpio_cfg.din = static_cast<gpio_num_t>(BoardConfig::ACTIVE.mic.data);
    if (i2s_channel_init_std_mode(b.tx, &cfg) != ESP_OK ||
        i2s_channel_init_std_mode(b.rx, &cfg) != ESP_OK) {
      destroyAudioBus();
      return false;
    }
  }
  slot = owner;
  return true;
}

inline bool startAudio(const void* owner, bool input) {
  auto& b = audioBus();
  if ((input ? b.microphone : b.speaker) != owner) return false;
  bool& enabled = input ? b.rxEnabled : b.txEnabled;
  if (enabled) return true;
  if (i2s_channel_enable(input ? b.rx : b.tx) != ESP_OK) return false;
  enabled = true;
  return true;
}

inline void stopAudio(const void* owner, bool input) {
  auto& b = audioBus();
  if ((input ? b.microphone : b.speaker) != owner) return;
  bool& enabled = input ? b.rxEnabled : b.txEnabled;
  if (enabled) i2s_channel_disable(input ? b.rx : b.tx);
  enabled = false;
}

inline void releaseAudio(const void* owner, bool input) {
  auto& b = audioBus();
  const void*& slot = input ? b.microphone : b.speaker;
  if (slot != owner) return;
  stopAudio(owner, input);
  slot = nullptr;
  if (!b.speaker && !b.microphone) destroyAudioBus();
}
} }  // namespace freeink::metalio
