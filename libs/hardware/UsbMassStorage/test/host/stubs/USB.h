#pragma once
inline bool usbBeginOk = true;
struct UsbStub { bool begin() { return usbBeginOk; } };
inline UsbStub USB;
