#include <HostRuntime.h>
#include <VolumeStorage.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include "check.h"
using namespace freeink;
int main() {
  char root[] = "/tmp/freeink-volumes-XXXXXX"; CHECK(mkdtemp(root));
  host::configureStorage(root);
  auto& storage = VolumeStorage::instance();
  CHECK(storage.begin());
  for (const auto* prefix : {"/tf", "/internal"}) {
    std::string path = std::string(prefix)+"/books/sub";
    CHECK(storage.ensureDirectoryExists(path.c_str()));
    path += "/book.txt";
    auto file = storage.open(path.c_str(), O_RDWR|O_CREAT|O_TRUNC);
    CHECK(file && file.write(prefix,strlen(prefix)) == strlen(prefix));
    auto volume = file.volume();
    CHECK(storage.openFiles(volume) == 1);
    CHECK(!storage.reserve(volume));
    StorageFile moved = std::move(file);
    CHECK(!file && moved && storage.openFiles(volume) == 1);
    CHECK(moved.seekSet(0) && moved.read() == '/');
    moved.close();
    CHECK(storage.openFiles(volume) == 0);
    auto dir = storage.open((std::string(prefix)+"/books/sub").c_str());
    auto child = dir.openNextFile(); char name[32];
    CHECK(child && child.getName(name,sizeof(name)) && std::string(name) == "book.txt");
    CHECK(storage.openFiles(volume) == 2);
    child.close(); dir.close();
    CHECK(storage.reserve(volume));
    CHECK(!storage.open(path.c_str()) && !storage.mount(volume));
    storage.release(volume);
    CHECK(storage.mount(volume) && storage.exists(path.c_str()));
    auto readonly = storage.open(path.c_str()); CHECK(readonly.write("x",1) == 0); readonly.close();
    auto writeonly = storage.open(path.c_str(),O_WRONLY); CHECK(writeonly.read() == -1); writeonly.close();
  }
  CHECK(!storage.rename("/tf/books/sub/book.txt", "/internal/moved.txt"));
  CHECK(!storage.open("/internal/../secret",O_CREAT|O_WRONLY));
  CHECK(!storage.open("/tf/./secret",O_CREAT|O_WRONLY));
  CHECK(!storage.open("/internal/missing",O_WRONLY));
  CHECK(storage.openFiles(StorageVolume::Tf) == 0 && storage.openFiles(StorageVolume::Internal) == 0);
  // Different callers share the same filesystem lock and own independent handles.
  auto write = [&](const char* path) {
    for (int i = 0; i < 40; ++i) {
      auto file = storage.open(path,O_WRONLY|O_CREAT|O_APPEND);
      if (!file || file.write("a",1) != 1) std::abort();
    }
  };
  std::thread a(write,"/tf/thread"), b(write,"/internal/thread"); a.join(); b.join();
  CHECK(storage.open("/tf/thread").fileSize() == 40);
  CHECK(storage.open("/internal/thread").fileSize() == 40);
  host::setDevice("faults", host::Json(host::Object{{"flash_mount",host::Json(true)}}));
  CHECK(!storage.mount(StorageVolume::Internal));
  CHECK(std::filesystem::exists(std::filesystem::path(root)/"flash/books/sub/book.txt"));
  host::setDevice("faults", host::Json(host::Object{}));
  CHECK(storage.mount(StorageVolume::Internal));
  std::filesystem::remove_all(root);
}
