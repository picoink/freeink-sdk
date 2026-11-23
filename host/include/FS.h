#pragma once

#include <cstdio>
#include <utility>

#include "SdFat.h"
#include "WString.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

// Basic Arduino file operations backed by an independent host storage volume.
// Stream helpers and raw flash/partition operations are outside this model.
namespace fs {
enum SeekMode { SeekSet, SeekCur, SeekEnd };
class File {
 public:
  File() = default;
  explicit operator bool() const { return bool(file_); }
  bool isDirectory() const { return file_.isDirectory(); }
  void close() { file_.close(); }
  int read() { return file_.read(); }
  size_t read(uint8_t* data, size_t size) {
    const int n = file_.read(data, size);
    return n > 0 ? size_t(n) : 0;
  }
  size_t readBytes(char* data, size_t size) { return read(reinterpret_cast<uint8_t*>(data), size); }
  size_t write(uint8_t value) { return file_.write(&value, 1); }
  size_t write(const uint8_t* data, size_t size) { return file_.write(data, size); }
  void flush() { file_.flush(); }
  bool sync() { return file_.sync(); }
  bool truncate(uint64_t n) { return file_.truncate(n); }
  uint64_t modificationTime() const { return file_.modificationTime(); }
  bool seek(uint32_t pos, SeekMode mode = SeekSet) {
    if (mode != SeekSet && mode != SeekCur && mode != SeekEnd) return false;
    return file_.seek(pos, mode == SeekSet ? SEEK_SET : mode == SeekCur ? SEEK_CUR : SEEK_END);
  }
  size_t position() const { return file_.curPosition(); }
  size_t size() const { return file_.fileSize(); }
  const char* name() const { return file_.name(); }
  const char* path() const { return file_.path(); }
  File openNextFile() { return File(file_.openNextFile()); }

 private:
  friend class LittleFSFS;
  explicit File(FsFile file) : file_(std::move(file)) {}
  FsFile file_;
};
}  // namespace fs

using fs::File;
using fs::SeekCur;
using fs::SeekEnd;
using fs::SeekMode;
using fs::SeekSet;
