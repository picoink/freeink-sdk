#pragma once
#include <fcntl.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
using oflag_t = int;
namespace freeink::host {
struct StorageVolume;
}
namespace fs {
class LittleFSFS;
}
struct FsBlockDeviceInterface {
  virtual ~FsBlockDeviceInterface() = default;
};
class FsFile {
 public:
  FsFile() = default;
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  FsFile(FsFile&&) = default;
  FsFile& operator=(FsFile&&) = default;
  explicit operator bool() const;
  bool isDirectory() const;
  void close();
  int read(void* data, size_t size);
  int read() {
    unsigned char c;
    return read(&c, 1) == 1 ? c : -1;
  }
  size_t write(const void* data, size_t size);
  bool seekSet(uint64_t pos);
  bool seek(int64_t offset, int origin);
  uint64_t curPosition() const;
  uint64_t fileSize() const;
  bool sync();
  void flush() { sync(); }
  bool getName(char* dst, size_t size) const;
  const char* name() const;
  const char* path() const;
  FsFile openNextFile();

 private:
  friend class SDCardManager;
  friend class fs::LittleFSFS;
  static FsFile hostOpen(freeink::host::StorageVolume& volume, const char* logical, int flags);
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
struct FsVolume {};
using SdFat = FsVolume;
