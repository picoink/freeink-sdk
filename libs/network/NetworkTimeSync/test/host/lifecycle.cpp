#include <cassert>

#include "fakes.h"
int main() {
  using Sync = freeink::NetworkTimeSync;
  auto& sync = Sync::instance();
  int64_t utc = 123;
  assert(sync.start() && sync.busy() && !sync.start());
  fake::task(nullptr);
  assert(!sync.busy() && sync.status().state == Sync::State::Succeeded);
  assert(sync.takeResult(utc) && utc == 1795392000LL && !sync.takeResult(utc));
  assert(!fake::sockets);
  assert(sync.start());
  sync.cancel();
  assert(!sync.start());
  fake::task(nullptr);
  assert(!sync.busy() && !sync.takeResult(utc) && !fake::sockets);
  fake::cancelReply = true;
  assert(sync.start());
  fake::task(nullptr);
  assert(sync.status().state == Sync::State::Idle && !sync.takeResult(utc) &&
         !fake::sockets);
  fake::cancelReply = false;
  fake::dnsOk = false;
  assert(sync.start());
  fake::task(nullptr);
  assert(sync.status().state == Sync::State::Failed && !sync.takeResult(utc) &&
         !fake::sockets);
  fake::dnsOk = true;
  fake::reply = false;
  auto before = fake::now;
  assert(sync.start());
  fake::task(nullptr);
  assert(fake::now - before == 30000 &&
         sync.status().state == Sync::State::Failed && !fake::sockets);
  fake::taskOk = false;
  assert(!sync.start() && !sync.busy());
  assert(sync.status().state == Sync::State::Failed && !sync.takeResult(utc));
  fake::taskOk = true;
  fake::reply = true;
  assert(sync.start());
  fake::task(nullptr);
  sync.cancel();
  assert(
      !sync.takeResult(utc));  // A queued success cannot escape cancellation.
}
