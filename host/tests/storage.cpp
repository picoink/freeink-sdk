#include <HostRuntime.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SDCardManager.h>
#include <ResourceCoordinator.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <set>

#include "check.h"

using namespace freeink::host;
namespace disk = std::filesystem;
std::string contents(const disk::path& path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeFile(const disk::path& path, const std::string& text) { std::ofstream(path) << text; }

int main() {
  char temporary[] = "/tmp/freeink-storage-XXXXXX";
  CHECK(mkdtemp(temporary));
  const disk::path base = disk::canonical(temporary), root = base / "storage";
  configureStorage(root.string());
  const auto& paths = runtime().storage;
  CHECK(paths.root == root && paths.tf == root / "tf" && paths.flash == root / "flash");
  CHECK(paths.nvs == root / "nvs" && paths.state == root / ".state");
  CHECK(disk::is_empty(paths.tf) && disk::is_empty(paths.flash) && disk::is_empty(paths.nvs));
  CHECK(SdMan.begin() && LittleFS.begin());
  auto sd = SdMan.open("/same.txt", O_RDWR | O_CREAT | O_TRUNC);
  auto flash = LittleFS.open("/same.txt", "w+");
  CHECK(sd && flash);
  CHECK(sd.write("tf", 2) == 2 && flash.write(reinterpret_cast<const uint8_t*>("flash"), 5) == 5);
  CHECK(sd.sync());
  flash.flush();
  CHECK(contents(root / "tf/same.txt") == "tf" && contents(root / "flash/same.txt") == "flash");
  CHECK(flash.size() == 5 && flash.position() == 5 && flash.seek(1));
  char data[8]{};
  CHECK(flash.readBytes(data, 3) == 3 && std::string(data) == "las");
  CHECK(std::string(flash.name()) == "same.txt" && std::string(flash.path()) == "/same.txt");
  CHECK(LittleFS.open("/same.txt", FILE_APPEND).write(uint8_t('!')) == 1);
  CHECK(contents(root / "flash/same.txt") == "flash!");
  CHECK(!LittleFS.open("/same.txt", "invalid"));

  // Model the S31 flash guard without hanging a host test indefinitely.
  using Coordinator=freeink::resources::Coordinator;
  auto& gate=Coordinator::get();
  setenv("FREEINK_HOST_CHECK_FLASH_BARRIER","1",1);
  gate.request(Coordinator::Barrier::Display,true);
  bool blocked=false;
  try { flash.seek(0); } catch (const std::runtime_error&) { blocked=true; }
  CHECK(blocked);
  gate.request(Coordinator::Barrier::Display,false);
  {
    Coordinator::Lease painting(Coordinator::Client::ForegroundIo);
    CHECK(painting);
    CHECK(!gate.request(Coordinator::Barrier::Display,true));
    CHECK(flash.seek(0) && flash.read()=='f');
  }
  CHECK(gate.ready());
  gate.request(Coordinator::Barrier::Display,false);
  unsetenv("FREEINK_HOST_CHECK_FLASH_BARRIER");

  CHECK(LittleFS.mkdir("/dir"));
  CHECK(LittleFS.open("/dir/a", FILE_WRITE).write(uint8_t('a')) == 1);
  CHECK(LittleFS.open("/nested/sub/file", FILE_WRITE, true));
  CHECK(LittleFS.rename("/dir/a", "/dir/b"));
  auto directory = LittleFS.open("/dir");
  CHECK(directory.isDirectory());
  auto entry = directory.openNextFile();
  CHECK(entry && !entry.isDirectory() && std::string(entry.name()) == "b");
  CHECK(!directory.openNextFile());
  CHECK(!LittleFS.rmdir("/dir"));
  CHECK(LittleFS.remove("/dir/b") && LittleFS.rmdir("/dir"));
  CHECK(!LittleFS.rmdir("/") && !LittleFS.rename("/", "/moved"));
  CHECK(!SdMan.remove("/") && !SdMan.rename("/", "/moved"));
  for (const auto& path : {paths.tf, paths.flash}) disk::create_directory_symlink(base, path + "/escape");
  CHECK(!LittleFS.open("/escape/secret", FILE_WRITE) && !LittleFS.open("/../secret", FILE_WRITE));
  CHECK(!SdMan.open("/escape/secret", O_CREAT | O_WRONLY));
  CHECK(!SdMan.open("/../secret", O_CREAT | O_WRONLY));
  CHECK(!LittleFS.rename("/same.txt", "/escape/moved"));
  CHECK(!LittleFS.mkdir("/escape/dir") && !disk::exists(base / "secret"));

  CHECK(SdMan.detachFilesystemForRawAccess());
  CHECK(!sd && sd.read(data, 1) == -1 && !SdMan.open("/same.txt"));
  CHECK(flash && flash.seek(0) && flash.read() == 'f');
  CHECK(SdMan.begin() && !sd);
  sd = SdMan.open("/same.txt");
  LittleFS.end();
  CHECK(!flash && !LittleFS.open("/same.txt") && sd && sd.read() == 't');
  CHECK(LittleFS.begin() && !flash);
  flash = LittleFS.open("/same.txt");
  setDevice("faults", Json(Object{{"sd", Json(true)}}));
  CHECK(!sd && flash && flash.read() == 'f');
  setDevice("faults", Json(Object{{"flash_write", Json(true)}}));
  CHECK(!LittleFS.open("/same.txt", FILE_WRITE) && !LittleFS.mkdir("/blocked"));
  CHECK(contents(root / "flash/same.txt") == "flash!" && sd);
  setDevice("faults", Json(Object{}));
  CHECK(SdMan.shutdown() && !sd && flash);
  CHECK(SdMan.begin() && !sd);

  Preferences first, second;
  CHECK(first.begin("one") && first.putUChar("brightness", 70) == 1);
  CHECK(first.putUShort("charge", 1500) == 2 && first.putBool("light", true) == 1);
  CHECK(second.begin("one") && second.getUChar("brightness") == 70);
  CHECK(second.putBool("other", true) == 1 && first.putUChar("brightness", 80) == 1);
  CHECK(second.begin("one") && second.getBool("other") && second.getUChar("brightness") == 80);
  CHECK(first.begin("two") && first.getUChar("brightness", 4) == 4);
  CHECK(first.putBool("light", false) == 1);
  CHECK(second.begin("one", true) && second.getBool("light"));
  CHECK(second.putUChar("brightness", 0) == 0 && second.getUShort("charge") == 1500);
  second.end();
  CHECK(second.putBool("light", false) == 0);
  CHECK(!second.begin("missing", true) && !second.begin("../bad"));
  CHECK(!second.putBool("light", true));
  CHECK(disk::is_regular_file(root / "nvs/one.json") && disk::is_regular_file(root / "nvs/two.json"));
  CHECK(disk::is_empty(paths.state));
  writeFile(root / "nvs/manual.json", "{\"brightness\":23,\"charge\":999999,\"light\":true}");
  CHECK(first.begin("manual") && first.getUChar("brightness") == 23 && first.getBool("light"));
  CHECK(first.getUShort("charge", 1000) == 1000);
  writeFile(root / "nvs/manual.json", "{broken");
  CHECK(first.putBool("light", false) == 0);
  CHECK(!first.begin("manual") && first.putBool("light", false) == 0);
  CHECK(contents(root / "nvs/manual.json") == "{broken");
  writeFile(base / "outside.json", "{}");
  disk::create_symlink(base / "outside.json", root / "nvs/linked.json");
  CHECK(!first.begin("linked") && first.putBool("light", true) == 0);
  CHECK(contents(base / "outside.json") == "{}");
  for (const auto& file : disk::directory_iterator(paths.nvs)) CHECK(file.path().extension() == ".json");

  LittleFS.end();
  SdMan.shutdown();
  first.end();
  second.end();
  disk::remove_all(base);
  std::cout << "Storage paths, independent Flash/TF lifetimes, file operations and NVS persistence passed\n";
}
