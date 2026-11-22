// POSIX fd and directory ownership adapted from CrossPoint's HalStorage.cpp.
// See third_party/crosspoint-LICENSE and host/README.md for provenance.
#include "SDCardManager.h"
#include "Preferences.h"
#include "HostMemory.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace fs=std::filesystem;
using namespace freeink::host;
namespace {
uint64_t generation=0;
bool detached=false;
std::string resolve(const char* logical) {
  if(!logical)return {};
  std::error_code ec;
  const auto root=fs::weakly_canonical(runtime().sdRoot,ec);if(ec)return {};
  fs::path rel=fs::path(logical).relative_path();
  for(const auto& part:rel)if(part=="..")return {};
  auto full=fs::weakly_canonical(root/rel,ec);if(ec)return {};
  auto r=root.begin(),p=full.begin();
  for(;r!=root.end();++r,++p)if(p==full.end()||*r!=*p)return {};
  return full.string();
}
}
struct FsFile::Impl {
  int fd=-1;DIR* dir=nullptr;std::string path;uint64_t epoch=0;
  ~Impl(){if(dir)closedir(dir);if(fd>=0)::close(fd);}
};
FsFile FsFile::hostOpen(const std::string& path,int flags){
  HostScope scope;FsFile f;if(path.empty()||detached||fault("sd"))return f;
  auto p=std::make_shared<Impl>();p->path=path;p->epoch=generation;
  struct stat st{};
  if(!stat(path.c_str(),&st)&&S_ISDIR(st.st_mode))p->dir=opendir(path.c_str());
  else p->fd=::open(path.c_str(),flags,0644);
  ioCost(0);if(p->fd>=0||p->dir)f.impl_=p;return f;
}
FsFile::operator bool()const{return impl_&&!detached&&impl_->epoch==generation&&!fault("sd");}
bool FsFile::isDirectory()const{return bool(*this)&&impl_->dir;}
void FsFile::close(){HostScope scope;impl_.reset();}
int FsFile::read(void* data,size_t n){HostScope scope;if(!*this||impl_->fd<0||fault("sd_read"))return -1;const auto got=::read(impl_->fd,data,n);ioCost(got>0?got:0);return int(got);}
size_t FsFile::write(const void* data,size_t n){HostScope scope;if(!*this||impl_->fd<0||fault("sd_write"))return 0;const auto put=::write(impl_->fd,data,n);ioCost(put>0?put:0);return put>0?size_t(put):0;}
bool FsFile::seekSet(uint64_t p){HostScope scope;if(!*this||impl_->fd<0)return false;ioCost(0);return lseek(impl_->fd,p,SEEK_SET)>=0;}
uint64_t FsFile::curPosition()const{return *this&&impl_->fd>=0?lseek(impl_->fd,0,SEEK_CUR):0;}
uint64_t FsFile::fileSize()const{struct stat s{};return *this&&impl_->fd>=0&&!fstat(impl_->fd,&s)?s.st_size:0;}
bool FsFile::sync(){return *this&&(!impl_->dir?fsync(impl_->fd)==0:true)&&!fault("sd_sync");}
bool FsFile::getName(char* dst,size_t size)const{HostScope scope;if(!impl_||!dst||!size)return false;auto n=fs::path(impl_->path).filename().string();std::snprintf(dst,size,"%s",n.c_str());return true;}
FsFile FsFile::openNextFile(){
  HostScope scope;if(!isDirectory())return {};
  while(auto* entry=readdir(impl_->dir)){
    if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
    std::error_code ec;auto logical=fs::relative(fs::path(impl_->path)/entry->d_name,runtime().sdRoot,ec);
    if(ec)continue;auto path=resolve(logical.string().c_str());if(!path.empty()){auto f=hostOpen(path,O_RDONLY);if(f)return f;}
  }return {};
}
SDCardManager& SDCardManager::getInstance(){static SDCardManager card;return card;}
bool SDCardManager::begin(){HostScope scope;if(fault("sd")||fault("sd_mount"))return mounted_=false;std::error_code ec;mounted_=fs::is_directory(runtime().sdRoot,ec);if(mounted_){detached_=detached=false;++generation;}return mounted_;}
bool SDCardManager::ready()const{return mounted_&&!detached_&&!fault("sd");}
FsFile SDCardManager::open(const char* path,int flags){HostScope scope;return ready()?FsFile::hostOpen(resolve(path),flags):FsFile{};}
bool SDCardManager::exists(const char* path){HostScope scope;auto p=resolve(path);std::error_code ec;return ready()&&!p.empty()&&fs::exists(p,ec);}
bool SDCardManager::remove(const char* path){HostScope scope;auto p=resolve(path);std::error_code ec;return ready()&&!p.empty()&&p!=runtime().sdRoot&&!fault("sd_write")&&fs::remove(p,ec);}
bool SDCardManager::rename(const char* a,const char* b){HostScope scope;auto from=resolve(a),to=resolve(b);if(!ready()||from.empty()||to.empty()||fault("sd_write"))return false;std::error_code ec;fs::rename(from,to,ec);return !ec;}
bool SDCardManager::ensureDirectoryExists(const char* path){HostScope scope;auto p=resolve(path);if(!ready()||p.empty())return false;std::error_code ec;fs::create_directories(p,ec);return !ec;}
std::vector<String> SDCardManager::listFiles(const char* path,size_t limit){HostScope scope;std::vector<String> result;auto dir=open(path);for(auto f=dir.openNextFile();f&&(!limit||result.size()<limit);f=dir.openNextFile()){char name[256];f.getName(name,sizeof(name));if(!f.isDirectory())result.emplace_back(name);}return result;}
FsBlockDeviceInterface* SDCardManager::detachFilesystemForRawAccess(){static FsBlockDeviceInterface dev;if(!ready())return nullptr;mounted_=false;detached_=detached=true;++generation;return &dev;}
bool SDCardManager::shutdown(){if(fault("sd_sync"))return false;mounted_=false;++generation;return true;}

bool Preferences::begin(const char* name,bool ro){HostScope scope;if(fault("preferences")||!name||std::strchr(name,'/'))return false;readOnly_=ro;path_=(fs::path(runtime().stateRoot)/(std::string(name)+".json")).string();std::ifstream in(path_);if(in){std::string data((std::istreambuf_iterator<char>(in)),{});if(!parseJson(data,values_).empty()||!values_.is<Object>())values_=Json(Object{});}return true;}
uint8_t Preferences::getUChar(const char* k,uint8_t v)const{return num(field(values_,k),v);}
uint16_t Preferences::getUShort(const char* k,uint16_t v)const{return num(field(values_,k),v);}
bool Preferences::getBool(const char* k,bool v)const{return boolean(field(values_,k),v);}
size_t Preferences::put(const char* key,Json value,size_t n){HostScope scope;if(readOnly_||path_.empty()||fault("preferences"))return 0;auto next=values_;next.get<Object>()[key]=value;std::ofstream out(path_+".tmp");out<<next.serialize();out.close();if(!out)return 0;std::error_code ec;fs::rename(path_+".tmp",path_,ec);if(ec)return 0;values_=std::move(next);return n;}
size_t Preferences::putUChar(const char* k,uint8_t v){return put(k,number(v),1);}
size_t Preferences::putUShort(const char* k,uint16_t v){return put(k,number(v),2);}
size_t Preferences::putBool(const char* k,bool v){return put(k,Json(v),1);}
