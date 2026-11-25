#include <EInkDisplay.h>
#include <FreeInkUIDisplayTarget.h>
#include <HostMemory.h>
#include <HostRuntime.h>
#include <InputManager.h>
#include <Preferences.h>
#include <PowerManager.h>
#include <SDCardManager.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "check.h"

int main() {
  using namespace freeink::host;
  namespace fs = std::filesystem;
  char path[] = "/tmp/freeink-host-XXXXXX";
  CHECK(mkdtemp(path));
  auto root = fs::path(path);
  configureStorage(root.string());
  runtime().outputRoot = (root / "output").string();
  CHECK(SdMan.begin());
  CHECK(!freeink::PowerManager::wokeFromSleep());
  using Receipt=freeink::PowerManager::WakeReceipt;
  CHECK(freeink::PowerManager::consumeWakeReceipt(42)==Receipt::Unavailable);
  freeink::PowerManager::armWakeReceipt(42);
  CHECK(freeink::PowerManager::consumeWakeReceipt(41)==Receipt::Unavailable);
  CHECK(freeink::PowerManager::consumeWakeReceipt(42)==Receipt::Ready);
  CHECK(freeink::PowerManager::consumeWakeReceipt(42)==Receipt::Consumed);
  freeink::PowerManager::armWakeReceipt(42);
  CHECK(freeink::PowerManager::consumeWakeReceipt(42)==Receipt::Ready);
  setDevice("wake_from_sleep", Json(true));
  CHECK(freeink::PowerManager::wokeFromSleep());
  setDevice("wake_from_sleep", Json(false));
  CHECK(!freeink::PowerManager::wokeFromSleep());
  CHECK(SdMan.ensureDirectoryExists("/books"));
  auto file = SdMan.open("/books/test.txt", O_RDWR | O_CREAT | O_TRUNC);
  CHECK(file);
  CHECK(file.write("abcdef", 6) == 6);
  CHECK(file.fileSize() == 6);
  CHECK(file.seekSet(2));
  char data[4]{};
  CHECK(file.read(data, 3) == 3);
  CHECK(std::string(data) == "cde");
  CHECK(file.sync());
  CHECK(!SdMan.open("/../outside", O_CREAT | O_RDWR));
  fs::create_directory_symlink(root, runtime().storage.tf + "/escape");
  CHECK(!SdMan.open("/escape/secret", O_CREAT | O_RDWR));
  CHECK(!fs::exists(root / "secret"));
  CHECK(SdMan.detachFilesystemForRawAccess());
  CHECK(!file);
  CHECK(file.read(data, 1) == -1);
  CHECK(!SdMan.open("/books/test.txt"));
  CHECK(SdMan.begin());
  CHECK(!file);
  CHECK(SdMan.open("/books/test.txt"));
  CHECK(SdMan.rename("/books/test.txt", "/books/moved.txt"));
  CHECK(!SdMan.exists("/books/test.txt"));
  Preferences prefs;
  CHECK(prefs.begin("test"));
  CHECK(prefs.putUChar("brightness", 70));
  CHECK(prefs.putBool("light", true));
  Preferences restored;
  CHECK(restored.begin("test"));
  CHECK(restored.getUChar("brightness") == 70);
  CHECK(restored.getBool("light"));
  InputManager input;
  CHECK(input.beginAsync());
  input.hostKey(InputManager::BTN_DOWN, true);
  input.hostKey(InputManager::BTN_DOWN, true);
  uint8_t key;
  CHECK(input.popPress(key) && key == InputManager::BTN_DOWN);
  CHECK(!input.popPress(key));
  input.hostKey(key, false);
  CHECK(!input.isPressed(key));
  input.hostHome(true);
  bool held;
  CHECK(input.popHomeKey(held) && held);
  CHECK(!input.popHomeKey(held));
  // A logical corner drawn by the production target must appear in that same
  // corner in exported pixels for every rotation, independent of SDL scaling.
  EInkDisplay display(-1, -1, -1, -1, -1, -1);
  display.begin();
  using namespace freeink::ui;
  for (auto orientation : {Orientation::Portrait, Orientation::PortraitInverted,
                           Orientation::LandscapeClockwise, Orientation::LandscapeCounterClockwise}) {
    display.clearScreen();
    DisplayTarget target(display.getFrameBuffer(), display.getDisplayWidth(), display.getDisplayHeight(),
                         display.getDisplayWidthBytes(), orientation);
    runtime().orientation = int(orientation);
    target.fill({0, 0, 5, 7}, Paint::solid(Color::Black));
    display.displayBuffer();
    auto f = snapshot();
    CHECK(f.pixels[0] == 0);
    CHECK(f.pixels[6 * f.width + 4] == 0);
    CHECK(f.pixels.back() == 255);
  }
  fs::remove_all(root);
  std::cout << "Host storage, preferences, input and four display rotations passed\n";
}
