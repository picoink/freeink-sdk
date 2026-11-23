// POSIX fd and directory ownership adapted from CrossPoint's HalStorage.cpp.
// See third_party/crosspoint-LICENSE and host/README.md for provenance.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <filesystem>
#include <limits>

#include "HostMemory.h"
#include "LittleFS.h"
#include "SDCardManager.h"

namespace disk = std::filesystem;
using namespace freeink::host;
namespace freeink::host {
struct StorageVolume {
  const char* faultPrefix;
  std::string root;
  uint64_t generation = 0;
  bool mounted = false;
  bool failed(const char* suffix = "") const {
    HostScope scope;
    return fault((std::string(faultPrefix) + suffix).c_str());
  }
  bool ready() const { return mounted && !failed(); }
  void cost(size_t bytes) const {
    if (!std::strcmp(faultPrefix, "sd")) ioCost(bytes);
  }
  void unmount() {
    mounted = false;
    ++generation;
  }
  bool mount(const std::string& path) {
    unmount();
    std::error_code ec;
    if (path.empty() || failed() || failed("_mount") || !disk::is_directory(path, ec)) return false;
    root = disk::canonical(path, ec).string();
    return mounted = !ec;
  }
};
}  // namespace freeink::host
namespace {
StorageVolume tf{"sd"}, flash{"flash"};
std::string resolve(const StorageVolume& volume, const char* logical) {
  if (!logical || volume.root.empty()) return {};
  std::error_code ec;
  const disk::path root(volume.root);
  const auto relative = disk::path(logical).relative_path();
  for (const auto& part : relative)
    if (part == "..") return {};
  const auto full = disk::weakly_canonical(root / relative, ec);
  if (ec) return {};
  auto r = root.begin(), p = full.begin();
  for (; r != root.end(); ++r, ++p)
    if (p == full.end() || *r != *p) return {};
  return full.string();
}
bool volumeExists(StorageVolume& volume, const char* logical) {
  HostScope scope;
  const auto path = resolve(volume, logical);
  std::error_code ec;
  return volume.ready() && !path.empty() && disk::exists(path, ec);
}
bool volumeRemove(StorageVolume& volume, const char* logical, bool directory = false) {
  HostScope scope;
  const auto path = resolve(volume, logical);
  std::error_code ec;
  if (!volume.ready() || path.empty() || path == volume.root || volume.failed("_write")) return false;
  if (disk::is_directory(path, ec) != directory || ec) return false;
  return disk::remove(path, ec);
}
bool volumeRename(StorageVolume& volume, const char* from, const char* to) {
  HostScope scope;
  const auto a = resolve(volume, from), b = resolve(volume, to);
  if (!volume.ready() || a.empty() || b.empty() || a == volume.root || b == volume.root ||
      volume.failed("_write"))
    return false;
  std::error_code ec;
  disk::rename(a, b, ec);
  return !ec;
}
bool volumeMkdir(StorageVolume& volume, const char* logical, bool recursive) {
  HostScope scope;
  const auto path = resolve(volume, logical);
  if (!volume.ready() || path.empty() || volume.failed("_write")) return false;
  std::error_code ec;
  if (disk::is_directory(path, ec)) return true;
  ec.clear();
  return recursive ? disk::create_directories(path, ec) : disk::create_directory(path, ec);
}
int openFlags(const char* mode) {
  if (!mode) return -1;
  if (!strcmp(mode, "r")) return O_RDONLY;
  if (!strcmp(mode, "r+")) return O_RDWR;
  if (!strcmp(mode, "w")) return O_WRONLY | O_CREAT | O_TRUNC;
  if (!strcmp(mode, "w+")) return O_RDWR | O_CREAT | O_TRUNC;
  if (!strcmp(mode, "a")) return O_WRONLY | O_CREAT | O_APPEND;
  if (!strcmp(mode, "a+")) return O_RDWR | O_CREAT | O_APPEND;
  return -1;
}
}  // namespace

void freeink::host::configureStorage(const std::string& root) {
  HostScope scope;
  if (root.empty()) throw std::runtime_error("Storage root must not be empty");
  disk::create_directories(root);
  StoragePaths paths;
  paths.root = disk::canonical(root).string();
  auto child = [&](const char* name) {
    const auto path = disk::path(paths.root) / name;
    disk::create_directories(path);
    return disk::canonical(path).string();
  };
  paths.nvs = child("nvs");
  paths.flash = child("flash");
  paths.tf = child("tf");
  paths.state = child(".state");
  tf.unmount();
  flash.unmount();
  runtime().storage = std::move(paths);
}
Json freeink::host::storageInfo() {
  const auto& p = runtime().storage;
  return Json(Object{{"root", Json(p.root)},
                     {"nvs", Json(p.nvs)},
                     {"flash", Json(p.flash)},
                     {"tf", Json(p.tf)},
                     {"state", Json(p.state)}});
}
struct FsFile::Impl {
  int fd = -1;
  DIR* dir = nullptr;
  std::string logical, name;
  StorageVolume* volume = nullptr;
  uint64_t epoch = 0;
  ~Impl() {
    if (dir) closedir(dir);
    if (fd >= 0) ::close(fd);
  }
};
FsFile FsFile::hostOpen(StorageVolume& volume, const char* logical, int flags) {
  HostScope scope;
  FsFile f;
  if (!volume.ready()) return f;
  const auto path = resolve(volume, logical);
  const bool writing = (flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC));
  if (path.empty() || volume.failed(writing ? "_write" : "_read")) return f;
  auto p = std::make_shared<Impl>();
  p->logical = (disk::path("/") / disk::path(logical).relative_path()).lexically_normal().string();
  p->name = disk::path(p->logical).filename().string();
  p->volume = &volume;
  p->epoch = volume.generation;
  struct stat st {};
  if (!stat(path.c_str(), &st) && S_ISDIR(st.st_mode)) {
    if (!writing) p->dir = opendir(path.c_str());
  } else {
    p->fd = ::open(path.c_str(), flags, 0644);
  }
  volume.cost(0);
  if (p->fd >= 0 || p->dir) f.impl_ = p;
  return f;
}
FsFile::operator bool() const {
  return impl_ && impl_->volume->ready() && impl_->epoch == impl_->volume->generation;
}
bool FsFile::isDirectory() const { return bool(*this) && impl_->dir; }
void FsFile::close() {
  HostScope scope;
  impl_.reset();
}
int FsFile::read(void* data, size_t n) {
  HostScope scope;
  if (!*this || impl_->fd < 0 || impl_->volume->failed("_read")) return -1;
  const auto got = ::read(impl_->fd, data, n);
  impl_->volume->cost(got > 0 ? got : 0);
  return int(got);
}
size_t FsFile::write(const void* data, size_t n) {
  HostScope scope;
  if (!*this || impl_->fd < 0 || impl_->volume->failed("_write")) return 0;
  const auto put = ::write(impl_->fd, data, n);
  impl_->volume->cost(put > 0 ? put : 0);
  return put > 0 ? size_t(put) : 0;
}
bool FsFile::seekSet(uint64_t pos) {
  return pos <= uint64_t(std::numeric_limits<int64_t>::max()) && seek(int64_t(pos), SEEK_SET);
}
bool FsFile::seek(int64_t offset, int origin) {
  HostScope scope;
  if (!*this || impl_->fd < 0) return false;
  impl_->volume->cost(0);
  return lseek(impl_->fd, offset, origin) >= 0;
}
uint64_t FsFile::curPosition() const { return *this && impl_->fd >= 0 ? lseek(impl_->fd, 0, SEEK_CUR) : 0; }
uint64_t FsFile::fileSize() const {
  struct stat s {};
  return *this && impl_->fd >= 0 && !fstat(impl_->fd, &s) ? s.st_size : 0;
}
bool FsFile::sync() {
  return *this && !impl_->volume->failed("_sync") && (impl_->dir || fsync(impl_->fd) == 0);
}
bool FsFile::getName(char* dst, size_t size) const {
  if (!*this || !dst || !size) return false;
  std::snprintf(dst, size, "%s", name());
  return true;
}
const char* FsFile::name() const { return *this ? impl_->name.c_str() : ""; }
const char* FsFile::path() const { return *this ? impl_->logical.c_str() : ""; }
FsFile FsFile::openNextFile() {
  HostScope scope;
  if (!isDirectory()) return {};
  while (auto* entry = readdir(impl_->dir)) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    const auto logical = (disk::path(impl_->logical) / entry->d_name).string();
    auto f = hostOpen(*impl_->volume, logical.c_str(), O_RDONLY);
    if (f) return f;
  }
  return {};
}
SDCardManager& SDCardManager::getInstance() {
  static SDCardManager card;
  return card;
}
bool SDCardManager::begin() {
  HostScope scope;
  return tf.mount(runtime().storage.tf);
}
bool SDCardManager::ready() const { return tf.ready(); }
FsFile SDCardManager::open(const char* path, int flags) { return FsFile::hostOpen(tf, path, flags); }
bool SDCardManager::exists(const char* path) { return volumeExists(tf, path); }
bool SDCardManager::remove(const char* path) { return volumeRemove(tf, path); }
bool SDCardManager::rename(const char* from, const char* to) { return volumeRename(tf, from, to); }
bool SDCardManager::ensureDirectoryExists(const char* path) { return volumeMkdir(tf, path, true); }
std::vector<String> SDCardManager::listFiles(const char* path, size_t limit) {
  HostScope scope;
  std::vector<String> result;
  auto dir = open(path);
  for (auto f = dir.openNextFile(); f && (!limit || result.size() < limit); f = dir.openNextFile()) {
    char name[256];
    f.getName(name, sizeof(name));
    if (!f.isDirectory()) result.emplace_back(name);
  }
  return result;
}
FsBlockDeviceInterface* SDCardManager::detachFilesystemForRawAccess() {
  static FsBlockDeviceInterface dev;
  if (!ready()) return nullptr;
  tf.unmount();
  return &dev;
}
bool SDCardManager::shutdown() {
  if (tf.failed("_sync")) return false;
  tf.unmount();
  return true;
}

fs::LittleFSFS LittleFS;
bool fs::LittleFSFS::begin(bool formatOnFail, const char*, uint8_t, const char*) {
  HostScope scope;
  // Mount the configured host directory; even formatOnFail never erases user files.
  // There is no partition image to format in this backend.
  (void)formatOnFail;
  return flash.mount(runtime().storage.flash);
}
void fs::LittleFSFS::end() { flash.unmount(); }
fs::File fs::LittleFSFS::open(const char* path, const char* mode, bool create) {
  HostScope scope;
  const int flags = openFlags(mode);
  if (flags < 0 || !path) return {};
  if (create && (flags & O_CREAT)) {
    const auto parent = disk::path(path).parent_path().string();
    if (!parent.empty() && !volumeMkdir(flash, parent.c_str(), true)) return {};
  }
  return File(FsFile::hostOpen(flash, path, flags));
}
bool fs::LittleFSFS::exists(const char* path) { return volumeExists(flash, path); }
bool fs::LittleFSFS::remove(const char* path) { return volumeRemove(flash, path); }
bool fs::LittleFSFS::rename(const char* from, const char* to) { return volumeRename(flash, from, to); }
bool fs::LittleFSFS::mkdir(const char* path) { return volumeMkdir(flash, path, false); }
bool fs::LittleFSFS::rmdir(const char* path) { return volumeRemove(flash, path, true); }
