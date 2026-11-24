#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <new>
namespace freeink::resources {
// Firmware participants announce work before touching shared storage. Display
// requests stop new work immediately, then wait for existing work to finish or
// acknowledge a safe point. No task is suspended while holding an unknown lock.
class Coordinator {
 public:
  enum class Client : uint8_t {Content,ForegroundIo,NetworkStorage,Count};
  enum class Barrier : uint8_t {Display=1,Usb=2,Sleep=4};
  static Coordinator& get() {
    alignas(Coordinator) static unsigned char storage[sizeof(Coordinator)];
    static auto* instance=new(storage) Coordinator;return *instance;
  }
  bool request(Barrier barrier,bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(enabled)barriers_|=uint8_t(barrier);else barriers_&=~uint8_t(barrier);
    return readyLocked();
  }
  bool blocked() {std::lock_guard<std::mutex> lock(mutex_);return barriers_!=0;}
  bool ready() {std::lock_guard<std::mutex> lock(mutex_);return readyLocked();}
  bool begin(Client client) {
    std::lock_guard<std::mutex> lock(mutex_);auto i=size_t(client);
    if(barriers_ || active_[i]==UINT16_MAX)return false;
    ++active_[i];return true;
  }
  void end(Client client) {std::lock_guard<std::mutex> lock(mutex_);auto i=size_t(client);if(active_[i])--active_[i];if(!active_[i])parked_[i]=false;}
  void park(Client client,bool value) {std::lock_guard<std::mutex> lock(mutex_);parked_[size_t(client)]=value;}
  class Lease {
   public:
    explicit Lease(Client client,bool needed=true):client_(client),held_(!needed || get().begin(client)),needed_(needed){}
    ~Lease(){if(held_ && needed_)get().end(client_);}
    explicit operator bool()const{return held_;}
    Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
   private:
    Client client_;bool held_,needed_;
  };
 private:
  bool readyLocked()const {for(size_t i=0;i<active_.size();++i)if(active_[i] && !parked_[i])return false;return true;}
  std::mutex mutex_;
  std::array<uint16_t,size_t(Client::Count)> active_{};
  std::array<bool,size_t(Client::Count)> parked_{};
  uint8_t barriers_=0;
};
}
