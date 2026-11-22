#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <fcntl.h>
using oflag_t = int;
struct FsBlockDeviceInterface { virtual ~FsBlockDeviceInterface() = default; };
class FsFile {
 public:
  FsFile() = default;
  static FsFile hostOpen(const std::string& path,int flags);
  explicit operator bool() const;
  bool isDirectory() const;
  void close();
  int read(void* data,size_t size);
  int read() { unsigned char c;return read(&c,1)==1?c:-1; }
  size_t write(const void* data,size_t size);
  bool seekSet(uint64_t pos);
  uint64_t curPosition() const;
  uint64_t fileSize() const;
  bool sync();
  void flush() { sync(); }
  bool getName(char* dst,size_t size) const;
  FsFile openNextFile();
 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
struct FsVolume {};
using SdFat = FsVolume;
