#pragma once
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

namespace freeink::detail {
struct InternalHandle {
  int descriptor = -1;
  DIR* directory = nullptr;
};
inline bool internalExists(const char* path) {
  struct stat info{};
  return stat(path, &info) == 0;
}
inline InternalHandle openInternal(const char* path, int flags) {
  struct stat info{};
  if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
    if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC))) return {};
    return {-1, opendir(path)};
  }
  // Use the descriptor directly. A temporary Arduino File opens a stdio
  // stream (and allocates libc mutexes) before reopening this same path.
  return {::open(path, flags, 0666), nullptr};
}
}
