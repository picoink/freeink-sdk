#pragma once
#include "SdFat.h"
#include "WString.h"
#include <vector>
class SDCardManager {
 public:
  static SDCardManager& getInstance();
  bool begin();
  bool ready() const;
  FsFile open(const char* path,oflag_t flags=O_RDONLY);
  FsFile open(const String& path,oflag_t flags=O_RDONLY) { return open(path.c_str(),flags); }
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* from,const char* to);
  bool ensureDirectoryExists(const char* path);
  std::vector<String> listFiles(const char* path,size_t maximum=0);
  FsBlockDeviceInterface* detachFilesystemForRawAccess();
  bool shutdown();
 private:
  bool mounted_=false,detached_=false;
};
#define SdMan SDCardManager::getInstance()
