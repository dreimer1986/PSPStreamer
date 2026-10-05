# Xbox / StreamMaster USB options (assessment, not implementation)

The current `streammaster/main/usb_bridge.c` is a USB host. The Xbox also has
a host controller; connecting those ports does not create a working device link.
The pinned nxdk includes OHCI/core/hub and XID support, with optional HID/CDC
class drivers (`lib/usb/Makefile`). That is a base for a custom transport, not
an existing Bluetooth stack or generic USB Wi-Fi driver.

## Preferred split

1. Keep Xbox Ethernet for media. For wireless placement, an Ethernet-to-Wi-Fi
   bridge avoids all Xbox Wi-Fi driver and USB bandwidth work.
2. If the Onju must provide Wi-Fi, build a separate ESP32-S3 **device-mode**
   firmware exposing our bounded socket protocol over vendor bulk USB. Reuse
   the Wi-Fi profiles/socket work and implement an Xbox host transport. The
   Xbox would not need a driver for the ESP Wi-Fi radio itself. This provides
   networking to PSPStreamer, not automatically every Xbox game/dashboard.
3. Bluetooth Classic remains a separate decision: with the ESP's OTG block in
   device mode it cannot also host the existing Bluetooth dongle. An extra PHY
   does not add another OTG controller. The S3's built-in Bluetooth is LE only.
   A dongle on the Xbox needs a USB-HCI transport plus a Bluetooth host/HID
   stack, pairing persistence and rumble mapping. Some protocol/parser logic
   can be reused, but the current ESP-IDF stack is not a drop-in nxdk driver.
   A second controller/board plus an inter-board link is another hardware option.

Do not start by porting arbitrary USB Wi-Fi dongle drivers: support would depend
on the particular chipset, firmware, security stack and network integration.
Nor does changing a cable make the S3's Full-Speed USB into High-Speed. Native
Ethernet is the useful performance baseline for the Xbox's higher video rates.
No firmware or USB ownership changes are included in the visualization fix.

Primary references:

- [Espressif USB FAQ: host and device cannot run simultaneously on S3 OTG](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html)
- [ESP32-S3 Bluetooth: LE, no Bluetooth Classic](https://docs.espressif.com/projects/esp-idf/en/v5.0.9/esp32s3/api-guides/ble/overview.html)
- [ESP32-S3 USB device driver](https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32s3/api-reference/peripherals/usb_device.html)
- [nxdk source and USB support](https://github.com/XboxDev/nxdk)
