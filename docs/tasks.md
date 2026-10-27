# PS3 Eye (OV534 / OV7720) driver for Windows 10 x64 — task board

Single source of truth for stages, acceptance criteria and current status.
Update this file when a stage starts, passes or is abandoned.

Target hardware: PlayStation Eye, `USB\VID_1415&PID_2000` (per-unit instance ID
deliberately not recorded here), bridge OV534, sensor OV7720 (non-UVC).

---

## Verified environment (observed, not assumed)

| Check | Command | Observed result |
|---|---|---|
| Secure Boot | `Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State` | `UEFISecureBootEnabled = 1` |
| WDK present | `Test-Path "C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\km"` | `False` (also no `stampinf.exe` / `inf2cat.exe` anywhere under `Windows Kits\10`) |
| Elevation | `WindowsPrincipal.IsInRole(Administrator)` | `False` (standard user, not elevated) |
| OS | `Get-CimInstance Win32_OperatingSystem` | `Microsoft Windows 10 Pro build 19045` |
| Device present | `Get-PnpDevice -PresentOnly` filtered on `VID_1415*PID_2000` | `Status OK`, `Class USBDevice`, `FriendlyName USB Camera-B4.09.24.1` |
| Driver bound | `Get-PnpDeviceProperty` | `DEVPKEY_Device_Service = WinUSB`, `ProblemCode = 0`, `DriverInfPath = oem56.inf` |
| Device interface GUID | `HKLM\...\Device Parameters\DeviceInterfaceGUIDs` | `{4BCC4C51-4249-4AE8-98BF-356B2C530E77}` |
| VS toolchain | `Test-Path "...\2019\BuildTools"` | `True` (MSVC v142 + SDK 19041) |
| cmake / ninja | `Get-Command` | absent (build uses `cl.exe` driven by PowerShell) |

---

## Correction to the original premise (evidence-backed)

The brief assumed the PS3 Eye streams over **isochronous** endpoints and that we
therefore need `WinUsb_RegisterIsochBuffer` / `WinUsb_ReadIsochPipeAsap`.

That is **not** the case for this device. Evidence:

1. `reference/ov534.c` (Linux `gspca_ov534`, v6.6), lines 1351-1353, for the
   OV772x path:
   ```c
   gspca_dev->cam.bulk = 1;
   gspca_dev->cam.bulk_size = 16384;
   gspca_dev->cam.bulk_nurbs = 2;
   ```
2. The `inspirit/PS3EYEDriver` sources (`src/ps3eye.cpp`, external checkout
   kept outside this repository — see `docs/REFERENCES.md`) — a working Win10
   implementation over libusb — picks the endpoint type by scanning interface 0,
   **alternate setting 0**, for `LIBUSB_TRANSFER_TYPE_BULK` (`find_ep`, lines
   260-292) and then submits `libusb_fill_bulk_transfer` with 64 KiB buffers
   (lines 762-776). It claims interface 0 at its default alt setting
   (`libusb_claim_interface(handle_, 0)`, line 1233).

Consequences, all favourable:

- No alternate-setting switch, no isochronous API, no libusb limitation to
  work around. The endpoint is a plain bulk IN pipe on interface 0 / alt 0.
- `libusb` on Windows would also suffice; WinUSB is kept because it removes a
  third-party dependency and is already bound to the device.
- The safety architecture (kernel = virtual camera only, USB stays in
  user-mode) is unchanged and still correct — it is simply cheaper to build.

Frame payload format (both sources agree):

- The bulk stream is a sequence of `2048`-byte chunks (16384 / 2048 = 8).
- Each chunk starts with a **12-byte UVC-style header**:
  `[0]=12`, `[1]=bmHeaderInfo` (EOH 0x80, ERR 0x40, PTP 0x10?, SCR 0x08,
  PTS 0x04, EOF 0x02, FID 0x01), `[2..5]=PTS` little endian, `[6..11]=SCR`.
- Payload per chunk = 2036 bytes. A frame ends on the chunk with `EOF` set.
- VGA YUY2 frame = 640*480*2 = 614400 bytes, so the last chunk is partial.

---

## Stage status

| Stage | Scope | Status |
|---|---|---|
| 0 | WDK install, Secure Boot off, test signing on | **BLOCKED — needs user action** (`docs/STAGE0-PREREQUISITES.md`) |
| 1 | Capture core + CLI frame grabber (no kernel, no admin) | **DONE** — all criteria met, see results below |
| 2 | AVStream virtual camera emitting a synthetic pattern | Not started (blocked by stage 0) |
| 3 | User-mode service pushing real frames into the driver | Not started |
| 4 | Controls + documentation | Not started |
| A | Microphone (UAC1, 4ch/16 kHz) | **DONE** — composite split (usbccgp) active, usbaudio.sys bound to MI_01, WinUSB on MI_00, video + mic verified |

---

## Stage 0 — Prerequisites

Owner: **user** (admin, BIOS/UEFI, reboot). Details and exact commands in
`docs/STAGE0-PREREQUISITES.md`.

Acceptance criteria:

- [ ] `Windows Kits\10\Include\10.0.19041.0\km` exists.
- [ ] `stampinf.exe`, `inf2cat.exe`, `signtool.exe` on PATH from a WDK prompt.
- [ ] `Confirm-SecureBootUEFI` returns `False` (or errors because Secure Boot
      is off).
- [ ] `bcdedit /enum {current}` reports `testsigning Yes` after reboot.
- [ ] `pnputil /enum-drivers` still lists the existing WinUSB binding for
      `VID_1415&PID_2000` (stage 1 must keep working).

Must not be executed by the agent.

---

## Stage 1 — Capture core + CLI frame grabber

No kernel code, no admin rights, no reboot. This is the risk-reduction stage:
it proves the sensor bring-up and the USB stream end to end.

### Deliverables

- `ps3eye-probe` — dumps the real USB descriptors, the WinUSB pipe map, the
  sensor ID and a handful of OV7720 registers.
- `ps3eye-grab` — initialises the OV534/OV7720 from the gspca register tables,
  streams over bulk IN, assembles frames, writes YUY2→PPM (or raw YUY2) to
  disk, reports fps / byte counts / error counters.
- `reference/ov534.c` — upstream Linux source, kept verbatim as provenance.

### Acceptance criteria

- [x] `ps3eye-probe` reports `Sensor ID 0x7720` (or 0x7721/0x7722/0x7723) and
      shows a bulk IN pipe on interface 0.
- [x] `ps3eye-grab --frames 30 --out out/` exits 0.
- [x] 30 PPM files exist, each 921600 bytes of RGB after a 614400-byte YUY2
      frame, and are visually non-black, non-garbage.
- [x] Reported frame rate ≥ 25 fps at 640x480 within the measured window.
- [x] `frames_ok / (frames_ok + frames_dropped) ≥ 0.9` over a 300-frame run.
- [x] Second consecutive run succeeds without replugging the camera
      (proves `0xe0=0x09` stop path releases the device cleanly).
- [x] 320x240 mode also produces valid frames (`--size 320x240`).

### Observed results (local unit, `USB\VID_1415&PID_2000`)

Sensor: `0x7721` (OV7720/OV7725 family), 0 USB/SCCB errors, init 1.89–2.27 s
(the init cost is `sccb_check_status`'s 10 ms status poll, inherent to the
Linux port, not a defect).

| Run | Result |
|---|---|
| Acceptance `--frames 30 --out out` | 30 written, 35 reassembled, **0 dropped**, 30.01 fps, 17.30 MB/s, all error counters 0 |
| Soak `--frames 300` | 300 written, 305 reassembled, **0 dropped**, 30.01 fps, 29.92 fps stream, 17.64 MB/s over 10.19 s |
| Immediate re-open, 30 frames | succeeded, 0 errors (clean `0xe0=0x09` stop) |
| QVGA `--size 320x240`, 20 frames | 20 written, 29.97 fps, 0 errors |
| Output files | 30 × PPM of 921,615 bytes = 15-byte header + 921,600 RGB; mean RGB 105.9/102.4/82.1, full 0..255 range; frame content visually confirmed |

Frame model verified exactly against the driver's accounting:
35 frames × 618,024 bytes (302 chunks × 2048, each carrying 2036 payload bytes)
= 21,630,840 bytes, which is the byte count the run reported.

### Known risks

- Sensor is not OV7720 on some units (OV7670 variant → JPEG). We detect the ID
  and abort loudly instead of streaming garbage.
- YUY2 register path is proven on Linux but the local unit has not been tested
  yet; the Bayer (`SGRBG8`) path used by `PS3EYEDriver` is the documented
  fallback if YUY2 fails twice.
- WinUSB is bound to the composite device node (no `usbccgp` children were
  enumerated). Interface 0 is what the handle is bound to; the UAC streaming
  interface 2 is *also* reachable through the same handle via
  `WinUsb_GetAssociatedInterface` — measured, see `docs/AUDIO.md`.

---

## Stage 2 — AVStream virtual camera with synthetic pattern

Blocked by stage 0.

Acceptance criteria:

- [ ] Minidriver loads with `ProblemCode 0`, appears under **Imaging devices**.
- [ ] Windows Camera app shows the synthetic pattern.
- [ ] `chrome://media-internals` / Edge `getUserMedia` shows the device and a
      live preview.
- [ ] OBS `Video Capture Device` lists it.
- [ ] Uninstall via `pnputil /delete-driver oemXX.inf /uninstall` leaves no
      residual device node.
- [ ] No bugcheck over a 10-minute idle + open/close loop.

## Stage 3 — Capture → driver bridge

- [ ] Service pushes real 640x480 YUY2 frames into the driver.
- [ ] Same three consumers as stage 2 show live camera video.
- [ ] 30-minute soak, no BSOD, no frame stall > 500 ms.
- [ ] Camera unplug while streaming does not bugcheck (graceful freeze).

## Stage 4 — Controls and documentation

- [ ] Exposure, gain, white balance, brightness, contrast, saturation, hue,
      sharpness, flip and frame rate mapped to camera properties.
- [ ] `docs/ARCHITECTURE.md`, `BUILD.md`, `INSTALL.md`, `UNINSTALL.md`,
      `PORTING.md`.

---

## Stage A — Microphone (UAC1, 4ch/16 kHz) — DONE

Completed via Option A (`usbccgp.sys` composite split).

### Observed results
- Composite parent: `USB\VID_1415&PID_2000` bound to Microsoft `usbccgp.sys`.
- Audio function: `USB\VID_1415&PID_2000&MI_01` bound to Microsoft `usbaudio.sys` (`Status: OK`).
- Windows CoreAudio endpoint: `Micrófono (USB Camera-B4.09.24.1)` active (`State: 1`) and set as default audio capture device.
- Video function: `USB\VID_1415&PID_2000&MI_00` bound to `WinUSB`. Dynamic GUID discovery added to `ps3eye_usb.c` to resolve both legacy and composite child interface GUIDs.
- `ps3eye-grab --frames 30` delivers 30/30 frames, 30.01 fps, 0 dropped frames, 0 errors simultaneously with the microphone active.

---

## Working rules

- One commit per verified milestone, on a feature branch. No push without
  explicit approval.
- Any step needing admin, BIOS or boot configuration is documented and waits
  for confirmation.
- Report observed output, never expectations.
- Two consecutive failures on the same stage → stop and report.

## Licensing note

`src/ov534.c` carries the OV7720/OV534 register tables and SCCB logic derived
from the Linux kernel file `drivers/media/usb/gspca/ov534.c`, which is
`GPL-2.0-or-later`. That file therefore stays under GPL-2.0-or-later. The
WinUSB plumbing and CLI are written from scratch. Because the final binaries
link both, the distributed capture core is effectively GPL-2.0-or-later.
Decision needed before any redistribution (not before local testing).
