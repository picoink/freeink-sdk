#include <AudioManager.h>
#include <BatteryMonitor.h>
#include <FrontlightManager.h>
#include <HapticManager.h>
#include <Imu.h>
#include <Microphone.h>
#include <PowerManager.h>
#include <Rtc.h>
#include <UsbMassStorage.h>

#include <cmath>
#include <ctime>
#include <fstream>

#include "EInkDisplay.h"
#include "HostMemory.h"
#include "HostRuntime.h"
#include "InputManager.h"
#include "Wire.h"

using namespace freeink::host;
namespace {
uint8_t registers[128][256]{};
uint64_t micStart = 0, micSamples = 0, audioEnd = 0, hapticEnd = 0;
freeink::AudioManager* playingAudio = nullptr;
bool hapticReady = false, hapticEnabled = false;
}  // namespace
uint8_t TwoWire::endTransmission(bool) {
  if ((address_ == 0x6b && fault("charger")) || (address_ == 0x20 && fault("expander"))) return 4;
  if (!used_) return 0;
  pointer_[address_] = tx_[0];
  for (unsigned i = 1; i < used_; ++i) {
    auto reg = uint8_t(tx_[0] + i - 1);
    registers[address_][reg] = tx_[i];
    if (address_ == 0x6b && reg == 0x70 && tx_[i] == 0x44) registers[address_][reg] = 3;
  }
  return 0;
}
uint8_t TwoWire::requestFrom(uint8_t address, uint8_t count, uint8_t) {
  address_ = address;
  reading_ = 0;
  if ((address == 0x6b && fault("charger")) || (address == 0x20 && fault("expander"))) return 0;
  return count;
}
int TwoWire::read() {
  auto reg = uint8_t(pointer_[address_] + reading_++);
  if (address_ == 0x20 && reg < 2) return reg == 0 ? 255 : (runtime().powerPressed ? 127 : 255);
  if (address_ == 0x6b && reg == 0x1e)
    return (deviceBool("charging", false) ? 8 : 0) | (deviceBool("external_power", false) ? 1 : 0);
  return registers[address_][reg];
}
uint16_t EInkDisplay::getDisplayWidth() const { return BoardConfig::ACTIVE.displayWidth; }
EInkDisplay::~EInkDisplay() { free(buffer_); }
uint16_t EInkDisplay::getDisplayHeight() const { return BoardConfig::ACTIVE.displayHeight; }
void EInkDisplay::begin() {
  if (!buffer_) buffer_ = static_cast<uint8_t*>(ps_malloc(getBufferSize()));
  if (!buffer_) throw std::runtime_error("Display framebuffer allocation failed");
  clearScreen();
}
void EInkDisplay::displayBufferAsync(RefreshMode mode) {
  publishFrame(buffer_, getDisplayWidth(), getDisplayHeight());
  busyUntil_ = nowMs() + (mode == FULL_REFRESH ? runtime().displayFullMs : runtime().displayFastMs);
}
void EInkDisplay::displayBuffer(RefreshMode mode, bool) {
  waitRefreshComplete();
  displayBufferAsync(mode);
  waitRefreshComplete();
}
bool EInkDisplay::refreshBusy() { return nowMs() < busyUntil_; }
void EInkDisplay::waitRefreshComplete() {
  while (refreshBusy()) delay(1);
}

BatteryMonitor::BatteryMonitor() : _adcPin(-1), _dividerMultiplier(1), _chargeStatusPin(-1) {}
BatteryMonitor::Status BatteryMonitor::readStatus() const {
  Status s;
  s.supported = true;
  if (fault("battery")) return s;
  s.percentageKnown = s.millivoltsKnown = s.chargingKnown = s.externalPowerKnown = true;
  s.percentage = deviceNumber("battery_percent", 75);
  s.millivolts = deviceNumber("battery_mv", 3900);
  s.charging = deviceBool("charging", false);
  s.externalPower = deviceBool("external_power", false);
  return s;
}
uint16_t BatteryMonitor::readPercentage() const { return readStatus().percentage; }
bool BatteryMonitor::readPercentageChecked(uint16_t& v) const {
  auto s = readStatus();
  if (!s.percentageKnown) return false;
  v = s.percentage;
  return true;
}
uint16_t BatteryMonitor::readMillivolts() const { return readStatus().millivolts; }
void FrontlightManager::begin() { _begun = !fault("frontlight"); }
void FrontlightManager::setBrightness(uint8_t n) {
  _brightness = std::min<int>(100, n);
  apply();
}
void FrontlightManager::setColorTemperature(uint8_t n) {
  _warmPercent = std::min<int>(100, n);
  apply();
}
void FrontlightManager::apply() {
  if (!_begun) return;
  setDevice("brightness", number(_brightness));
  setDevice("warmth", number(_warmPercent));
}
void FrontlightManager::prepareForDeepSleep() { setBrightness(0); }
namespace freeink {
bool AudioManager::begin() { return begun_ = !fault("audio"); }
bool AudioManager::present() const { return begun_; }
void AudioManager::setVolume(uint8_t n) {
  volume_ = std::min<int>(100, n);
  setDevice("volume", number(volume_));
}
bool AudioManager::playBuffer(const uint8_t* data, size_t len, bool loop) {
  HostScope scope;
  if (!begun_ || fault("audio") || len < 44 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4))
    return false;
  std::ofstream out(runtime().outputRoot + "/audio.wav", std::ios::binary);
  out.write(reinterpret_cast<const char*>(data), len);
  out.close();
  if (!out) return false;
  playing_ = true;
  playingAudio = this;
  audioEnd = loop ? UINT64_MAX : nowMs() + (len - 44) * 1000 / 32000;
  setDevice("audio_playing", Json(true));
  setDevice("audio_bytes", number(len));
  return true;
}
void AudioManager::stop() {
  playing_ = false;
  if (playingAudio == this) playingAudio = nullptr;
  setDevice("audio_playing", Json(false));
}
void AudioManager::powerDown() {
  stop();
  begun_ = false;
}
bool Microphone::begin(uint32_t rate) {
  sampleRate_ = rate;
  micStart = nowMs();
  micSamples = 0;
  begun_ = !fault("microphone");
  setDevice("microphone_active", Json(begun_));
  return begun_;
}
int Microphone::read(int16_t* dst, size_t n, uint32_t) {
  if (!begun_ || fault("microphone_read")) return -1;
  if (fault("microphone_stall")) return 0;
  const uint64_t available = (nowMs() - micStart) * sampleRate_ / 1000;
  n = std::min<uint64_t>(n, available > micSamples ? available - micSamples : 0);
  for (size_t i = 0; i < n; ++i)
    dst[i] = int16_t(4000 * std::sin(2 * 3.141592653589793 * 220 * (micSamples + i) / sampleRate_));
  micSamples += n;
  return int(n);
}
void Microphone::end() {
  begun_ = false;
  setDevice("microphone_active", Json(false));
}
bool Imu::begin() { return begun_ = !fault("imu"); }
bool Imu::wake() {
  sleeping_ = false;
  return begun_ && !fault("imu");
}
bool Imu::sleep() {
  sleeping_ = true;
  return begun_;
}
bool Imu::silenceInterrupt() {
  sleeping_ = true;
  return !fault("imu_interrupt");
}
bool Imu::read(Sample& s) {
  if (!begun_ || sleeping_ || fault("imu")) return false;
  s = {float(deviceNumber("ax", 0)), float(deviceNumber("ay", 0)), float(deviceNumber("az", 1)), 0, 0, 0};
  return true;
}
bool Rtc::begin() { return begun_ = !fault("rtc"); }
bool Rtc::now(DateTime& out) {
  if (!begun_ || fault("rtc") || !deviceBool("rtc_valid", true)) return false;
  const time_t t = runtime().rtcEpoch + (nowMs() - runtime().rtcSetAt) / 1000;
  struct tm v {};
  gmtime_r(&t, &v);
  out = {uint16_t(v.tm_year + 1900), uint8_t(v.tm_mon + 1), uint8_t(v.tm_mday), uint8_t(v.tm_hour),
         uint8_t(v.tm_min),          uint8_t(v.tm_sec),     uint8_t(v.tm_wday)};
  return true;
}
bool Rtc::set(const DateTime& d) {
  if (!begun_ || fault("rtc_write")) return false;
  struct tm v {};
  v.tm_year = d.year - 1900;
  v.tm_mon = d.month - 1;
  v.tm_mday = d.day;
  v.tm_hour = d.hour;
  v.tm_min = d.minute;
  v.tm_sec = d.second;
  runtime().rtcEpoch = timegm(&v);
  runtime().rtcSetAt = nowMs();
  setDevice("rtc_valid", Json(true));
  return true;
}
bool Rtc::armWakeTimer(uint32_t seconds) {
  if (!begun_ || fault("rtc_timer")) return false;
  runtime().wakeAt = nowMs() + seconds * 1000;
  return true;
}
bool Rtc::cancelWakeTimer() {
  runtime().wakeAt = 0;
  runtime().rtcFired = false;
  return !fault("rtc");
}
bool Rtc::readWakeTimer(bool& fired) {
  fired = runtime().rtcFired;
  return begun_;
}
HapticManager& HapticManager::getInstance() {
  static HapticManager h;
  return h;
}
bool HapticManager::supported() const { return true; }
bool HapticManager::begin() { return hapticReady = !fault("haptic"); }
bool HapticManager::present() const { return hapticReady; }
void HapticManager::setEnabled(bool enabled) {
  hapticEnabled = enabled;
  setDevice("haptic_enabled", Json(enabled));
  if (!enabled) stop();
}
bool HapticManager::enabled() const { return hapticEnabled; }
bool HapticManager::pulse(uint32_t ms, uint8_t intensity) {
  if (!hapticReady || !hapticEnabled || fault("haptic")) return false;
  hapticEnd = nowMs() + ms;
  setDevice("haptic_pulses", number(deviceNumber("haptic_pulses", 0) + 1));
  setDevice("haptic_intensity", number(intensity));
  return true;
}
void HapticManager::stop() {
  hapticEnd = 0;
  setDevice("haptic_intensity", number(0));
}
bool HapticManager::isPlaying() const { return hapticEnd > nowMs(); }
bool UsbMassStorage::begin(FsBlockDeviceInterface* d) {
  if (!d || fault("usb")) return false;
  _active = true;
  setDevice("usb_state", number(int(UsbMassStorageState::WaitingForHost)));
  return true;
}
void UsbMassStorage::end() {
  _active = false;
  setDevice("usb_state", number(0));
}
UsbMassStorageState UsbMassStorage::state() const {
  return _active ? static_cast<UsbMassStorageState>(int(deviceNumber("usb_state", 1)))
                 : UsbMassStorageState::Idle;
}
bool UsbMassStorage::hostConnected() const {
  return state() == UsbMassStorageState::Connected || state() == UsbMassStorageState::Accessed;
}
bool PowerManager::armPowerButtonWakeup(bool) { return !fault("wake"); }
void PowerManager::powerDownRailsForSleep() {}
PowerManager::AbortedSleepInfo PowerManager::takeAbortedSleepInfo() { return {}; }
void PowerManager::waitForPowerButtonRelease() {}
void PowerManager::deepSleep() {
  runtime().sleeping = true;
  throw Sleep{};
}
void PowerManager::deepSleepUntilPowerButton() { deepSleep(); }
}  // namespace freeink
namespace freeink::host {
void serviceDevices() {
  if (playingAudio && nowMs() >= audioEnd) playingAudio->stop();
  if (hapticEnd && nowMs() >= hapticEnd) HapticManager::getInstance().stop();
}
}  // namespace freeink::host
