#include "../../../BluetoothPan/test/host/fakes.h"
#include <BluetoothHost.h>
using Host = freeink::BluetoothHost;
int main() {
  auto pan = Host::Profile::Pan, hid = Host::Profile::Hid;
  assert(Host::acquire(pan));
  assert(fake::lastMode == BT_MODE_BTDM);
  assert(Host::acquire(hid));
  assert(Host::acquire(pan));
  assert(fake::starts == 1);
  assert(Host::release(pan));
  assert(fake::bluedroid == 2 && !fake::stops && Host::held(hid));
  fake::stopOk = false;
  assert(!Host::release(hid));
  assert(Host::held(hid));
  assert(!Host::acquire(pan));
  fake::stopOk = true;
  assert(Host::release(hid));
  assert(!Host::held(hid) && !fake::controller);
  assert(Host::acquire(hid));
  assert(Host::acquire(pan));
  assert(Host::release(hid));
  assert(fake::bluedroid == 2 && Host::held(pan));
  assert(Host::release(pan));
  fake::hostEnableOk = false;
  assert(!Host::acquire(pan));
  assert(!Host::held(pan) && !fake::controller && !fake::bluedroid);
  fake::hostEnableOk = true;
  assert(Host::acquire(pan));
  assert(Host::release(pan));
  fake::controller = 2;
  assert(!Host::acquire(pan));
  assert(!Host::held(pan) && fake::controller == 2);
  assert(Host::release(pan) &&
         fake::controller == 2); // An external host is never stopped.
}
