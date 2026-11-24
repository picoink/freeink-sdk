#include "InternalFile.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

using freeink::detail::internalExists;
using freeink::detail::openInternal;
int main() {
  char root[] = "/tmp/freeink-internal-XXXXXX";
  assert(mkdtemp(root));
  const std::string path = std::string(root) + "/font.ttf";
  assert(!internalExists(path.c_str()));
  auto missing = openInternal(path.c_str(), O_RDONLY);
  assert(missing.descriptor < 0 && !missing.directory);
  auto created = openInternal(path.c_str(), O_RDWR | O_CREAT | O_EXCL);
  assert(created.descriptor >= 0 && !created.directory);
  assert(write(created.descriptor, "font", 4) == 4);
  assert(fsync(created.descriptor) == 0);
  assert(internalExists(path.c_str()));
  assert(openInternal(path.c_str(), O_WRONLY | O_CREAT | O_EXCL).descriptor < 0);
  assert(close(created.descriptor) == 0);
  auto append = openInternal(path.c_str(), O_WRONLY | O_APPEND);
  assert(append.descriptor >= 0 && lseek(append.descriptor, 0, SEEK_SET) == 0);
  assert(write(append.descriptor, "!", 1) == 1);
  assert(close(append.descriptor) == 0);
  for (int i = 0; i < 100; ++i) {
    auto read = openInternal(path.c_str(), O_RDONLY);
    char bytes[8]{};
    assert(read.descriptor >= 0 && ::read(read.descriptor, bytes, sizeof(bytes)) == 5);
    assert(!strcmp(bytes, "font!"));
    assert(write(read.descriptor, "x", 1) < 0);
    assert(close(read.descriptor) == 0);
  }
  auto truncated = openInternal(path.c_str(), O_RDWR | O_TRUNC);
  struct stat info{};
  assert(truncated.descriptor >= 0 && !fstat(truncated.descriptor, &info) && info.st_size == 0);
  assert(close(truncated.descriptor) == 0);
  auto directory = openInternal(root, O_RDONLY);
  assert(directory.descriptor < 0 && directory.directory);
  bool found = false;
  while (auto* entry = readdir(directory.directory)) found |= !strcmp(entry->d_name, "font.ttf");
  assert(found && !closedir(directory.directory));
  assert(!openInternal(root, O_WRONLY).directory);
  assert(!openInternal(root, O_RDONLY | O_CREAT | O_EXCL).directory);
  assert(!unlink(path.c_str()) && !rmdir(root));
}
