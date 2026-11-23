#pragma once

#include "FS.h"

namespace fs {
class LittleFSFS {
 public:
  bool begin(bool formatOnFail = false, const char* basePath = "/littlefs", uint8_t maxOpenFiles = 10,
             const char* partitionLabel = "spiffs");
  void end();
  File open(const char* path, const char* mode = FILE_READ, bool create = false);
  File open(const String& path, const char* mode = FILE_READ, bool create = false) {
    return open(path.c_str(), mode, create);
  }
  bool exists(const char* path);
  bool exists(const String& path) { return exists(path.c_str()); }
  bool remove(const char* path);
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to);
  bool rename(const String& from, const String& to) { return rename(from.c_str(), to.c_str()); }
  bool mkdir(const char* path);
  bool mkdir(const String& path) { return mkdir(path.c_str()); }
  bool rmdir(const char* path);
  bool rmdir(const String& path) { return rmdir(path.c_str()); }
};
}  // namespace fs

extern fs::LittleFSFS LittleFS;
