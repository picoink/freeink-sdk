#include <VolumeStorage.h>
#include <SDCardManager.h>
#include <cstring>
#include <utility>
#ifdef FREEINK_HOST
#include <LittleFS.h>
#define INTERNAL_FS LittleFS
#elif FREEINK_INTERNAL_FAT
#include <FFat.h>
#define INTERNAL_FS FFat
#endif

namespace freeink {
namespace { using Lock = std::lock_guard<std::recursive_mutex>; }
std::recursive_mutex& storageMutex() { static std::recursive_mutex mutex; return mutex; }
VolumeStorage& VolumeStorage::instance() { static VolumeStorage storage; return storage; }
bool VolumeStorage::begin() {
  mount(StorageVolume::Internal); mount(StorageVolume::Tf); return ready();
}
bool VolumeStorage::mount(StorageVolume volume) {
  Lock lock(storageMutex());
  if (reserved_[unsigned(volume)] || users_[unsigned(volume)]) return false;
  if (volume == StorageVolume::Tf) return SdMan.begin();
#ifdef INTERNAL_FS
  // Never format when mounting fails. Images are prepared explicitly off-device.
  internalReady_ = INTERNAL_FS.begin(false, "/internal", 24, "storage");
#endif
  return internalReady_;
}
bool VolumeStorage::ready(StorageVolume volume) const {
  Lock lock(storageMutex());
  return !reserved_[unsigned(volume)] && (volume == StorageVolume::Tf ? SdMan.ready() : internalReady_);
}
StorageVolume VolumeStorage::defaultVolume() const {
  return ready(StorageVolume::Tf) ? StorageVolume::Tf : StorageVolume::Internal;
}
StoragePath VolumeStorage::resolve(const char* name) const {
  StoragePath result;
  if (!name || name[0] != '/' || strlen(name) > 255 || strchr(name, '\\')) return result;
  result.volume = defaultVolume();
  if (!strncmp(name, "/internal", 9) && (!name[9] || name[9] == '/')) {
    result.volume = StorageVolume::Internal; name += 9;
  } else if (!strncmp(name, "/tf", 3) && (!name[3] || name[3] == '/')) {
    result.volume = StorageVolume::Tf; name += 3;
  }
  result.path = *name ? name : "/";
  for (size_t start = 1; start < result.path.size();) {
    auto end = result.path.find('/', start);
    auto part = result.path.substr(start, end == std::string::npos ? end : end-start);
    if (part.empty() || part == "." || part == "..") return result;
    if (end == std::string::npos) break;
    start = end+1;
  }
  result.valid = true; return result;
}
StorageFile VolumeStorage::open(const char* name, oflag_t flags) {
  Lock lock(storageMutex()); StorageFile file;
  auto p = resolve(name); if (!p.valid || !ready(p.volume)) return file;
  if (p.volume == StorageVolume::Tf) file.sd_ = SdMan.open(p.path.c_str(), flags);
#ifdef INTERNAL_FS
  else {
    const bool exists = INTERNAL_FS.exists(p.path.c_str());
    if ((!exists && !(flags & O_CREAT)) || (exists && (flags & O_EXCL) && (flags & O_CREAT))) return file;
    const bool write = (flags & O_ACCMODE) != O_RDONLY;
    const char* mode = !write ? "r" : (flags & O_APPEND) ? "a+" :
      ((flags & O_TRUNC) || !exists) ? "w+" : "r+";
    file.flash_ = INTERNAL_FS.open(p.path.c_str(), mode);
  }
#endif
  file.adopt(p.volume,flags); return file;
}
bool VolumeStorage::exists(const char* name) {
  Lock lock(storageMutex()); auto p = resolve(name); if (!p.valid || !ready(p.volume)) return false;
  if (p.volume == StorageVolume::Tf) return SdMan.exists(p.path.c_str());
#ifdef INTERNAL_FS
  return INTERNAL_FS.exists(p.path.c_str());
#else
  return false;
#endif
}
bool VolumeStorage::remove(const char* name) {
  Lock lock(storageMutex()); auto p = resolve(name); if (!p.valid || !ready(p.volume) || p.path == "/") return false;
  if (p.volume == StorageVolume::Tf) return SdMan.remove(p.path.c_str());
#ifdef INTERNAL_FS
  return INTERNAL_FS.remove(p.path.c_str());
#else
  return false;
#endif
}
bool VolumeStorage::rename(const char* from, const char* to) {
  Lock lock(storageMutex()); auto a = resolve(from), b = resolve(to);
  if (!a.valid || !b.valid || a.volume != b.volume || !ready(a.volume) || a.path == "/" || b.path == "/") return false;
  if (a.volume == StorageVolume::Tf) return SdMan.rename(a.path.c_str(), b.path.c_str());
#ifdef INTERNAL_FS
  return INTERNAL_FS.rename(a.path.c_str(), b.path.c_str());
#else
  return false;
#endif
}
bool VolumeStorage::ensureDirectoryExists(const char* name) {
  Lock lock(storageMutex()); auto p = resolve(name); if (!p.valid || !ready(p.volume)) return false;
  if (p.volume == StorageVolume::Tf) return SdMan.ensureDirectoryExists(p.path.c_str());
#ifdef INTERNAL_FS
  for (size_t end = 1; end <= p.path.size(); ++end) {
    if (end != p.path.size() && p.path[end] != '/') continue;
    auto part = p.path.substr(0, end);
    if (!INTERNAL_FS.exists(part.c_str()) && !INTERNAL_FS.mkdir(part.c_str())) return false;
  }
  return true;
#else
  return false;
#endif
}
uint32_t VolumeStorage::openFiles(StorageVolume volume) const { Lock lock(storageMutex()); return users_[unsigned(volume)]; }
bool VolumeStorage::reserved(StorageVolume volume) const { Lock lock(storageMutex()); return reserved_[unsigned(volume)]; }
bool VolumeStorage::reserve(StorageVolume volume) {
  Lock lock(storageMutex());
  if (!ready(volume) || users_[unsigned(volume)]) return false;
  reserved_[unsigned(volume)] = true;
#ifdef INTERNAL_FS
  if (volume == StorageVolume::Internal) { INTERNAL_FS.end(); internalReady_ = false; }
#endif
  return true;
}
void VolumeStorage::release(StorageVolume volume) { Lock lock(storageMutex()); reserved_[unsigned(volume)] = false; }
void StorageFile::adopt(StorageVolume volume, oflag_t flags) {
  volume_ = volume; flags_ = flags;
  counted_ = volume == StorageVolume::Tf ? bool(sd_) : bool(flash_);
  if (counted_) ++VolumeStorage::instance().users_[unsigned(volume_)];
}
bool StorageFile::valid() const {
  return counted_ && VolumeStorage::instance().ready(volume_) &&
    (volume_ == StorageVolume::Tf ? bool(sd_) : bool(flash_));
}
StorageFile::StorageFile(StorageFile&& other) { *this = std::move(other); }
StorageFile& StorageFile::operator=(StorageFile&& other) {
  if (&other == this) return *this;
  Lock lock(storageMutex()); close();
  sd_ = std::move(other.sd_); flash_ = std::move(other.flash_);
  volume_ = other.volume_; flags_ = other.flags_; counted_ = other.counted_; other.counted_ = false;
  return *this;
}
StorageFile::operator bool() const { Lock lock(storageMutex()); return valid(); }
bool StorageFile::isDirectory() const { Lock lock(storageMutex()); return valid() && (volume_ == StorageVolume::Tf ? sd_.isDirectory() : flash_.isDirectory()); }
void StorageFile::close() {
  Lock lock(storageMutex());
  if (!counted_) return;
  if (volume_ == StorageVolume::Tf) sd_.close(); else flash_.close();
  --VolumeStorage::instance().users_[unsigned(volume_)]; counted_ = false;
}
int StorageFile::read(void* data, size_t length) {
  Lock lock(storageMutex()); if (!valid() || !data || (flags_ & O_ACCMODE) == O_WRONLY) return -1;
  return volume_ == StorageVolume::Tf ? sd_.read(data,length) : int(flash_.read(static_cast<uint8_t*>(data),length));
}
int StorageFile::read() { uint8_t b; return read(&b,1) == 1 ? b : -1; }
size_t StorageFile::write(const void* data, size_t length) {
  Lock lock(storageMutex()); if (!valid() || !data || (flags_ & O_ACCMODE) == O_RDONLY) return 0;
  return volume_ == StorageVolume::Tf ? sd_.write(data,length) : flash_.write(static_cast<const uint8_t*>(data),length);
}
bool StorageFile::seekSet(uint64_t position) {
  Lock lock(storageMutex()); if (!valid()) return false;
  return volume_ == StorageVolume::Tf ? sd_.seekSet(position) : position <= UINT32_MAX && flash_.seek(uint32_t(position));
}
uint64_t StorageFile::curPosition() const { Lock lock(storageMutex()); return !valid() ? 0 : volume_ == StorageVolume::Tf ? sd_.curPosition() : flash_.position(); }
uint64_t StorageFile::fileSize() const { Lock lock(storageMutex()); return !valid() ? 0 : volume_ == StorageVolume::Tf ? sd_.fileSize() : flash_.size(); }
bool StorageFile::sync() {
  Lock lock(storageMutex()); if (!valid()) return false;
  if (volume_ == StorageVolume::Tf) return sd_.sync();
  flash_.flush(); return true;
}
bool StorageFile::getName(char* out, size_t length) const {
  Lock lock(storageMutex()); if (!valid() || !out || !length) return false;
  if (volume_ == StorageVolume::Tf) return sd_.getName(out,length);
  const char* name = flash_.name(); const char* base = name ? strrchr(name,'/') : nullptr;
  return snprintf(out,length,"%s",base ? base+1 : name ? name : "") < int(length);
}
StorageFile StorageFile::openNextFile() {
  Lock lock(storageMutex()); StorageFile next;
  if (!valid()) return next;
  if (volume_ == StorageVolume::Tf) next.sd_ = sd_.openNextFile(); else next.flash_ = flash_.openNextFile();
  next.adopt(volume_); return next;
}
}
