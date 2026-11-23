#include <InputManager.h>
#include <Wire.h>
#include <cassert>
#include <cstdio>
int main() {
  for (int& level : gpioLevels) level = HIGH;
  InputManager input;
  input.begin();
  for (int failure = 1; failure <= 9; ++failure) {
    queueCreates = 0; failQueueAt = failure;
    input.beginAsync(2, 15, 32, true);
    assert(!input.asyncRunning() && liveQueues == 0);
  }
  failQueueAt = 0; failTask = true;
  input.beginAsync(); assert(!input.asyncRunning() && liveQueues == 0);
  failTask = false;
  // A held cover HOME is reported via the async queue, not a racy frame flag.
  Wire.touch = {1, 0, 80, 3, 132}; // raw (80, 900)
  gpioLevels[1] = LOW; touchIsr();
  input.setHomeLongPressMs(500);
  input.beginAsync(2, 1, 32, true);
  bool held = false, received = false;
  const auto start = millis();
  while (millis() - start < 2500 && !received) { received = input.popHomeKey(held); delay(5); }
  assert(received && held && millis()-start < 1000);
  freeink::OrderedInput event{};
  assert(input.popEvent(event) && event.kind == freeink::OrderedInput::Home && event.pressed);
  assert(!input.popEvent(event));
  assert(input.endAsync() && !input.asyncRunning() && liveQueues == 0);
  while (liveTasks) delay(1);
  input.beginAsync(2, 100);
  while (!inTaskDelay) delay(1);
  assert(!input.endAsync(0) && input.asyncRunning()); // Timeout cancels the stop request.
  assert(input.endAsync(1000) && liveQueues == 0);
  while (liveTasks) delay(1);
  input.beginAsync(); assert(input.asyncRunning() && input.endAsync());
  while (liveTasks) delay(1);
  assert(liveQueues == 0 && input.endAsync());
  puts("Async input HOME, allocation failures, cooperative stop, timeout and restart passed");
}
