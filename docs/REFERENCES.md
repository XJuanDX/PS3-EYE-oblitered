# External references

This repository contains **no third-party checkouts**. The projects below were
read on the development machine to validate the protocol; they live outside the
working tree on purpose. Only `reference/ov534.c` and `reference/gspca.h` are
vendored, because they are the canonical source for the register tables and are
required to build `src/ov534.c`.

## Linux kernel — `gspca_ov534`

- Upstream: `drivers/media/usb/gspca/ov534.c`, fetched from `v6.6`.
- Vendored verbatim in `reference/ov534.c`.
- License: `GPL-2.0-or-later`. See `docs/tasks.md` → *Licensing note*.
- Used for: bridge/sensor register tables, SCCB sequences, frame assembly
  rules, control register mappings.

## inspirit/PS3EYEDriver

- https://github.com/inspirit/PS3EYEDriver
- Uses libusb. Independent implementation of the same device.
- Used to cross-check and confirm: bulk IN endpoint on interface 0 alt 0,
  2048-byte UVC-style payload headers, 12-byte header layout.
- Not vendored; read-only reference.

## jkevin/PS3EyeDirectShow

- https://github.com/jkevin/PS3EyeDirectShow
- DirectShow source filter for the same camera on Windows.
- Relevant as a cross-check for Win10 behaviour and for the libwdi/WinUSB
  binding approach. Its `baseclasses` copy is the Microsoft DirectShow base
  classes redistributed in that repo, not used by this project.
- Not vendored; read-only reference.

## PSCam4Windows (Media Foundation virtual camera)

- Windows 11 22H2+ only (`IMFVirtualCamera`).
- Relevant to `docs/PORTING.md` (stage 4), not to the Windows 10 path.
- Not vendored.

## Microsoft

- WDK / AVStream documentation: https://learn.microsoft.com/windows-hardware/drivers/stream/
- Windows driver samples: https://github.com/microsoft/Windows-driver-samples
- WinUSB API: https://learn.microsoft.com/windows/win32/api/winusb/

## Reference material that is *not* committed

Full local checkouts of the projects above are intentionally absent from this
repository. Absolute paths and machine-specific identifiers are never recorded
in tracked files.
