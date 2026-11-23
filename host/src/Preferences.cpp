#include "Preferences.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include "HostMemory.h"

using namespace freeink::host;
namespace disk = std::filesystem;
namespace {
std::mutex preferencesMutex;
bool validName(const char* name) {
  return name && *name && std::strlen(name) <= 15 && !std::strchr(name, '/') && !std::strchr(name, '\\');
}
bool load(const std::string& path, Json& values) {
  values = Json(Object{});
  std::error_code ec;
  const auto status = disk::symlink_status(path, ec);
  if (status.type() == disk::file_type::not_found) return true;
  // Namespace files must be regular files in nvs/, not links to another volume.
  if (ec || !disk::is_regular_file(status)) {
    std::cerr << "NVS: cannot read namespace file " << path << '\n';
    return false;
  }
  std::ifstream in(path);
  if (!in) {
    std::cerr << "NVS: cannot open " << path << '\n';
    return false;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), {});
  if (in.bad() || !parseJson(text, values).empty() || !values.is<Object>()) {
    std::cerr << "NVS: invalid JSON object in " << path << "; file preserved\n";
    values = Json(Object{});
    return false;
  }
  return true;
}
bool save(const std::string& path, const Json& values) {
  const std::string text = values.serialize(true);
  std::string temporary = path + ".tmp-XXXXXX";
  const int fd = mkstemp(temporary.data());
  if (fd < 0) return false;
  size_t written = 0;
  while (written < text.size()) {
    const auto count = ::write(fd, text.data() + written, text.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    written += size_t(count);
  }
  bool ok = written == text.size() && fsync(fd) == 0;
  if (::close(fd) != 0) ok = false;
  std::error_code ec;
  if (ok) {
    disk::rename(temporary, path, ec);
    ok = !ec;
  }
  if (!ok) disk::remove(temporary, ec);
  return ok;
}
template <class T>
T unsignedValue(const Json& values, const char* key, T fallback) {
  if (!key) return fallback;
  const auto& value = field(values, key);
  if (!value.is<double>()) return fallback;
  const double n = value.get<double>();
  return std::isfinite(n) && n >= 0 && n <= std::numeric_limits<T>::max() && std::floor(n) == n ? T(n)
                                                                                                : fallback;
}
}  // namespace

void Preferences::end() {
  HostScope scope;
  path_.clear();
  values_ = Json(Object{});
  readOnly_ = true;
}
bool Preferences::begin(const char* name, bool ro) {
  HostScope scope;
  end();
  if (fault("preferences") || !validName(name) || runtime().storage.nvs.empty()) return false;
  std::lock_guard<std::mutex> lock(preferencesMutex);
  const auto path = (disk::path(runtime().storage.nvs) / (std::string(name) + ".json")).string();
  std::error_code ec;
  if (ro && !disk::exists(path, ec)) return false;
  if (!load(path, values_)) return false;
  path_ = path;
  readOnly_ = ro;
  return true;
}
uint8_t Preferences::getUChar(const char* key, uint8_t value) const {
  return unsignedValue(values_, key, value);
}
uint16_t Preferences::getUShort(const char* key, uint16_t value) const {
  return unsignedValue(values_, key, value);
}
bool Preferences::getBool(const char* key, bool value) const {
  return key ? boolean(field(values_, key), value) : value;
}
size_t Preferences::put(const char* key, Json value, size_t bytes) {
  HostScope scope;
  if (readOnly_ || path_.empty() || !validName(key) || fault("preferences")) return 0;
  std::lock_guard<std::mutex> lock(preferencesMutex);
  Json next;
  // Merge the latest file so separate handles cannot overwrite unrelated keys.
  if (!load(path_, next)) return 0;
  next.get<Object>()[key] = std::move(value);
  if (!save(path_, next)) {
    std::cerr << "NVS: could not save " << path_ << '\n';
    return 0;
  }
  values_ = std::move(next);
  return bytes;
}
size_t Preferences::putUChar(const char* key, uint8_t v) { return put(key, number(v), 1); }
size_t Preferences::putUShort(const char* key, uint16_t v) { return put(key, number(v), 2); }
size_t Preferences::putBool(const char* key, bool v) { return put(key, Json(v), 1); }
