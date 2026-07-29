# candle API (gs_usb / candleLight Windows driver)

Vendored copy of the candle Windows API used by SavvyCAN's gs_usb
connection (`connections/gs_usb.cpp`). It talks to gs_usb class devices
(candleLight, CANable, CANnectivity, cantact, ...) over WinUSB, so no vendor
driver is needed — only a WinUSB binding on the device interface (which the
stock candleLight/CANable firmware already declares via WCID descriptors).

## Provenance

- Original: <https://github.com/HubertD/candle_dll> — Copyright (c) 2016 Hubert Denkmair
- This copy: <https://github.com/Schildkroet/CANgaroo>, `src/driver/CandleApiDriver/api/`
  at commit `ed3088d5393e02a01afe867af2ee8ab0e12db039`, which adds CAN FD,
  multi-channel and hardware timestamp support — Copyright (c) 2026 Schildkroet

## Local changes

Kept to the minimum needed to build here; re-apply them when refreshing from upstream.

- `candle.h` / `candle.c`: the three functions returning a pointer declared the calling
  convention before the `*` (`wchar_t __stdcall DLL *candle_dev_get_path`). MinGW accepts
  that, MSVC rejects it with C2165, so `__stdcall` moved behind the `*`.

## License

These files are **LGPL-3.0**, not MIT like the rest of SavvyCAN. They are only
compiled into Windows builds. Keep `LICENSE` alongside them, and keep the files
separable so the LGPL relinking provision can be satisfied. Do not copy code out
of this directory into the MIT-licensed parts of the tree.
