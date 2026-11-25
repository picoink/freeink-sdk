#pragma once
#include <FS.h>
#include <SdFat.h>
#include <cstdint>
#include <mutex>
#include <string>

namespace freeink {
enum class StorageVolume : uint8_t { Tf, Internal };
struct StorageIoStats {uint64_t opens=0,reads=0,bytesRead=0,entries=0,cacheHits=0;};
StorageIoStats storageIoStats(StorageVolume);
void storageCacheHit(StorageVolume);
struct StoragePath {
  StorageVolume volume = StorageVolume::Tf;
  std::string path;
  bool valid = false;
};
// One lock covers both filesystem implementations, including file operations.
// The firmware can safely move book work to a worker without sharing FAT caches.
std::recursive_mutex& storageMutex();
class VolumeStorage;
class StorageFile {
 public:
  StorageFile() = default;
  ~StorageFile() { close(); }
  StorageFile(const StorageFile&) = delete;
  StorageFile& operator=(const StorageFile&) = delete;
  StorageFile(StorageFile&&);
  StorageFile& operator=(StorageFile&&);
  explicit operator bool() const;
  bool isDirectory() const;
  void close();
  int read(void*, size_t);
  int read();
  size_t write(const void*, size_t);
  bool seekSet(uint64_t);
  // Optional read-only hint. SdFat can avoid walking the FAT chain for every
  // backwards glyph seek after verifying a bounded file is contiguous.
  bool optimizeReadSeeks();
  uint64_t curPosition() const;
  uint64_t fileSize() const;
  uint64_t size() const { return fileSize(); }
  bool sync();
  bool truncate(uint64_t);
  // Stable modification token; zero means the backend has no timestamp.
  uint64_t modificationTime() const;
  void flush() { sync(); }
  bool getName(char*, size_t) const;
  StorageFile openNextFile();
  bool iterationFailed() const;
  StorageVolume volume() const { return volume_; }
 private:
  friend class VolumeStorage;
  void adopt(StorageVolume, oflag_t = O_RDONLY);
  bool valid() const;
  mutable FsFile sd_;
  mutable fs::File flash_;
  StorageVolume volume_ = StorageVolume::Tf;
  bool counted_ = false;
  oflag_t flags_ = O_RDONLY;
  int descriptor_ = -1;
  void* directory_ = nullptr;
  bool iterationError_ = false;
  std::string path_;
};
class VolumeStorage {
 public:
  static VolumeStorage& instance();
  bool begin();
  bool mount(StorageVolume);
  bool ready(StorageVolume) const;
  bool ready() const { return ready(StorageVolume::Tf) || ready(StorageVolume::Internal); }
  StorageVolume defaultVolume() const;
  StoragePath resolve(const char*) const;
  StorageFile open(const char*, oflag_t = O_RDONLY);
  bool exists(const char*);
  bool remove(const char*);
  bool rename(const char*, const char*);
  bool ensureDirectoryExists(const char*);
  uint32_t openFiles(StorageVolume) const;
  // Reserve a volume for USB only when every StorageFile has closed. A failed
  // reservation does not unmount or invalidate any live file.
  bool reserve(StorageVolume);
  void release(StorageVolume);
  bool reserved(StorageVolume) const;
 private:
  friend class StorageFile;
  bool internalReady_ = false, reserved_[2]{};
  uint32_t users_[2]{};
};
}
