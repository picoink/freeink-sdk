#include <ResourceCoordinator.h>
#include <cassert>
#include <thread>
using freeink::resources::Coordinator;
int main() {
  auto& gate=Coordinator::get();
  using Client=Coordinator::Client;using Barrier=Coordinator::Barrier;
  assert(gate.begin(Client::Content));
  assert(!gate.request(Barrier::Display,true));
  assert(!gate.begin(Client::ForegroundIo));
  assert(!gate.begin(Client::NetworkStorage));
  // A pause request is not acknowledgement of an in-flight read/write.
  std::thread worker([&]{gate.park(Client::Content,true);});worker.join();
  assert(gate.ready());
  assert(gate.request(Barrier::Usb,true));
  gate.request(Barrier::Display,false);assert(gate.blocked());
  assert(!gate.begin(Client::Content));
  gate.request(Barrier::Usb,false);assert(!gate.blocked());
  gate.park(Client::Content,false);assert(!gate.ready());gate.end(Client::Content);
  {
    Coordinator::Lease write(Client::NetworkStorage);assert(write);
    assert(!gate.request(Barrier::Sleep,true));
  }
  assert(gate.ready());gate.request(Barrier::Sleep,false);
  {
    Coordinator::Lease native(Client::ForegroundIo);assert(native);
    assert(!gate.request(Barrier::Display,true));
  }
  assert(gate.ready());gate.request(Barrier::Display,false);
}
