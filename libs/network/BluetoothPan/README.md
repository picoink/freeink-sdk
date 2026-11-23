# Bluetooth PAN client

`freeink::BluetoothPan::supported()` is a side-effect-free capability query.
It is true only for a build with Classic Bluetooth hardware, Bluedroid, Classic,
PAN and `FREEINK_CAP_BLUETOOTH_PAN=1`. It does not open NVS, start tasks or turn on
the radio. The host and unsupported builds return false; commands return
`Unsupported`. Arduino 4.0 RC1 on S31 needs the
[PAN library build](../../../tools/bluetooth-pan/README.md).

The singleton provides asynchronous `begin`, `scan`, `connect(address)`,
`confirmPairing(accept)`, `disconnect`, `forget`, `end` and a snapshot `status`.
`Ok` means a command was accepted, not that the link has Internet access.
Callers must poll status. Commands and snapshots may be called by the application
loop; stack callbacks and frame delivery are owned by the driver worker.

- PANU connects to a selected NAP. SSP numeric comparison must be confirmed on
  both devices within 30 seconds. Other peers and legacy PIN requests are rejected.
- Discovery keeps up to 16 devices. Callbacks copy bounded data; eight Ethernet
  frames of at most 1514 bytes may be queued. Congested TX fails immediately.
  Dropped data frames are counted; loss of a control event shuts down safely.
- A successful DHCP lease persists the last peer in the `freeink-pan` NVS namespace.
  `begin` reconnects to it; link/connection failures retry at 2/5/10/30 seconds.
  Initialization/connect watchdog failures stop the radio and require enabling
  again. No background task runs before `begin`.
- `end`, `disconnect` and `forget` drain PAN and stop Bluedroid/controller. PAN has
  no connect-cancel API, so disconnect also turns the radio off. `forget` removes
  the current/saved bond and peer preferences. A late connection is rejected.
- Wait until `status.enabled == false` before starting another radio transport
  or sleeping. Repeated `end` is safe, including before startup has completed.
  Controller memory is retained for later enables in the same boot.

The caller owns Wi-Fi exclusion, UI, sleep policy and applying time. For a
connectivity check, `freeink::NetworkTimeSync` queries `ntp.aliyun.com` on UDP 123.
It returns UTC through `takeResult`, without changing system time or the RTC.
`cancel` discards both queued and late results; wait for `busy() == false` before
starting another query. DNS and socket work run in a separate worker. Requests
time out after 30 seconds, except that an in-progress resolver call must return
before its worker can exit. Invalid/unsynchronized NTP replies are rejected.

Host verification:

```sh
python3 libs/network/BluetoothPan/test/host/run.py
python3 libs/network/NetworkTimeSync/test/host/run.py
```

Radio/RTOS fakes exercise the actual driver worker/callbacks and NTP worker.
They do not establish real Bluetooth interoperability or verify power consumption.
