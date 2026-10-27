# Audio (microphone) — feasibility assessment

Status: **assessed, not decided**. Nothing in this document changes the video
path, and no driver was modified to produce it.

Question: the PlayStation Eye also contains a microphone. Can it be included,
and at what cost?

---

## 1. What the device declares

Decoded from the live configuration descriptor by
`ps3eye-probe --descriptors-only` (observed output, not datasheet claims):

| Item | Value |
|---|---|
| Audio function | UAC1, `bcdADC 0x0100`, 1 streaming interface |
| INPUT_TERMINAL 1 | type `0x0201` (microphone), **4 channels**, channel config `0x0000` |
| FEATURE_UNIT 3 | source 1, control size 1, bitmaps `[0]00 [1]02 [2]02 [3]02 [4]02` (per-channel volume, no master control) |
| OUTPUT_TERMINAL 2 | type `0x0101` (USB streaming), source unit 3 |
| AS_GENERAL (alt 1) | terminal link 2, format tag `0x0001` (PCM) |
| AS_FORMAT_TYPE_I | 4 channels, 2 bytes/sample, 16 bit, one rate: **16000 Hz** |
| Streaming endpoint | `EP 0x84 IN isochronous`, max packet 768, interval 4 |
| CS_ENDPOINT | `bmAttributes 0x01` → sampling-frequency control supported |

Derived byte rate: 4 × 2 × 16000 = **128,000 B/s** (1.02 Mbit/s), about 0.7%
of the video stream's ~18.4 MB/s. Bandwidth is not the constraint.

The configuration has **no IAD** (`bNumInterfaces = 3`, no `0x0B` descriptor):

| Interface | Class | Contents |
|---|---|---|
| 0 | `ff/00/00` | video: EP 0x81 bulk IN, EP 0x02 bulk OUT, EP 0x83 interrupt IN |
| 1 | `01/01/00` | audio control, no endpoints |
| 2 | `01/02/00` | audio streaming: alt 0 empty, alt 1 has EP 0x84 isochronous |

---

## 2. What Windows exposes today

Observed on this machine: the camera is a **single** device node
(`USB\VID_1415&PID_2000\...`, class `USBDevice`, service `WinUSB`,
`oem56.inf` installed by libwdi). There are no `MI_00/MI_01/MI_02` children
(so `usbccgp` is not in the path), no `usbaudio` service bound to this VID,
and no audio endpoint that belongs to the camera. The "Micrófono (USB Audio
CODEC)" endpoint present in the system belongs to a different device.

So today the microphone is dead weight: no application can see it.

---

## 3. What was tested, and what came out

### 3.1 Reachability through the current WinUSB handle

`ps3eye-probe --descriptors-only`:

| Call | Result |
|---|---|
| `WinUsb_QueryInterfaceSettings(iface, 0)` | bound interface 0, class `ff/00/00` |
| `WinUsb_GetAssociatedInterface(h, 1)` | **succeeds**, returns `bInterfaceNumber 2` (audio streaming, alt 0) |
| `WinUsb_GetAssociatedInterface(h, 2)` | fails, error 259 (`ERROR_NO_MORE_ITEMS`) |

Useful and unexpected: the audio **streaming** interface is reachable from the
existing handle even without an IAD. The audio **control** interface (1) is
not, so mute/volume cannot be driven this way — only endpoint-level controls.

`ps3eye-probe --audio-alt` (the only write the probe performs, undone before
it returns):

| Call | Result |
|---|---|
| `WinUsb_QueryInterfaceSettings(a, 1)` | interface 2 alt 1, 1 endpoint |
| `WinUsb_SetCurrentAlternateSetting(a, 1)` | ok |
| `WinUsb_QueryPipe(a, 1, 0)` | EP 0x84, isochronous, max packet 768, interval 4 |
| `WinUsb_SetCurrentAlternateSetting(a, 0)` | ok — device restored |

### 3.2 Actual audio read

`ps3eye-audio` runs the full documented WinUSB isochronous sequence:
`WinUsb_RegisterIsochBuffer`, `WinUsb_ReadIsochPipeAsap`, wait on the
overlapped event, `WinUsb_GetOverlappedResult`.

Every attempt fails:

| Variant | Result |
|---|---|
| 8, 64, 256, 1024, 2048 packets | `WinUsb_GetOverlappedResult` → **error 31** (`ERROR_GEN_FAILURE`) |
| plus `SET_CUR` sampling frequency 16000 Hz on EP 0x84 (accepted, 3 bytes) | same error 31 |
| plus the video stream running and drained concurrently | same error 31 |

`WinUsb_RegisterIsochBuffer` and `WinUsb_ReadIsochPipeAsap` both report
success / `ERROR_IO_PENDING`; the request is accepted and the transfer then
completes as failed. The alternate setting is restored to 0 in every run, and
a normal video capture immediately afterwards still works (5 frames, 0 errors).

Error 31 is the generic mapping of a failed URB. From user mode the exact
`USBD_STATUS` is not visible, so **the root cause cannot be determined** with
the evidence available.

Hypotheses, explicitly unproven:

- WinUSB expects one *function* per handle, described by an IAD or by
  `usbccgp`. Here it is bound to a whole three-interface device with neither,
  so the associated-interface handle may exist without being a schedulable
  function from the USB core's point of view.
- The device may need vendor-specific setup before the audio endpoint runs.
  The PS3 Eye's audio is only known to work under the Linux host stack.

---

## 4. Options

### A. Rebind the device so `usbccgp` splits it — recommended if audio is wanted

Install a WinUSB INF that targets only the video interface (`MI_00`) and let
the standard composite driver own the device node. Then:

- Windows' own `usbaudio.sys` binds to interfaces 1+2, so the microphone
  appears as a normal capture endpoint with **zero audio code from us**;
- our WinUSB video code keeps working, opening `...&MI_00`.

Cost: admin (driver install). Risk: medium, because it changes the topology
that the working Stage 1 depends on. Reversible: re-run Zadig / Device Manager
to restore `oem56.inf` on the whole device, which is exactly the current state.

Not verified: whether `usbccgp` will group this **IAD-less** configuration into
"vendor function + audio function" as desired. Microsoft's composite driver
does have class-based grouping heuristics for audio, but that must be proven
on this unit. If it lumps everything into one function, audio still will not
appear and option A buys nothing.

### B. Keep WinUSB and fight the isochronous path

Blocked at the first read (error 31). `WinUsb_ReadIsochPipe` with explicit
frame numbers is the only untried variant; it needs a frame-number source and
does not address the unknown root cause. Low expected value.

### C. Do not include audio

The deliverable is a camera. The microphone is a separate USB function with a
class driver that Windows already ships; the obstacle is the current single
device-node binding, not missing code.

---

## 5. Recommendation

Finish the camera path (stages 2-4) first. If audio is still wanted afterwards,
run option A as an isolated experiment with the rollback ready: it is the only
path that can yield the microphone without writing an audio driver, and its
outcome is binary and fast to observe.

---

## 6. Not verified (explicit list)

- Whether `usbccgp` splits this IAD-less device into the two functions we want.
- Whether `usbaudio.sys` accepts these descriptors (a 4-channel input terminal
  with `wChannelConfig 0` is unusual).
- Whether all four declared channels carry signal or only one.
- Why the WinUSB isochronous read fails with error 31.
- Whether any vendor-specific setup is required to start the microphone.
- Latency, clock drift and A/V sync — untested, no data.
