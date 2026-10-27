/* ps3eye_usb.h — WinUSB transport for the PlayStation Eye (OV534 bridge).
 *
 * The device is bound to WinUSB by a libwdi/Zadig INF (oem56.inf), so there is
 * no kernel driver of ours in the path. Video streams over a plain bulk IN
 * pipe on interface 0, alternate setting 0 — see docs/tasks.md for the
 * evidence behind that statement.
 */
#ifndef PS3EYE_USB_H
#define PS3EYE_USB_H

#include <windows.h>
#include <winusb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define PS3EYE_VID 0x1415u
#define PS3EYE_PID 0x2000u

/* Device interface class GUID installed by oem56.inf. */
extern const GUID PS3EYE_IFACE_GUID;

typedef struct {
    HANDLE                  file;
    WINUSB_INTERFACE_HANDLE iface;
    uint8_t                 bulk_in_pipe;
    uint16_t                bulk_in_max_packet;
    uint8_t                 bus_speed;
    bool                    opened;
} ps3eye_dev;

/* Open the first PS3 Eye bound to WinUSB. Returns false and fills `err`. */
bool ps3eye_open(ps3eye_dev *d, char *err, size_t errlen);
void ps3eye_close(ps3eye_dev *d);

void ps3eye_format_error(char *buf, size_t len, const char *what);

/* Raw vendor control transfers. `request` is bRequest; `value`/`index` are
 * wValue/wIndex. Direction is implied by the function, not by `request`. */
bool ps3eye_ctrl_out(ps3eye_dev *d, uint8_t request, uint16_t value,
                     uint16_t index, const void *data, uint32_t len);
bool ps3eye_ctrl_in(ps3eye_dev *d, uint8_t request, uint16_t value,
                    uint16_t index, void *data, uint32_t len,
                    uint32_t *transferred);

/* Human-readable dump of the configuration descriptor and the WinUSB pipe
 * map. Read-only: performs no vendor requests. */
void ps3eye_dump_descriptors(ps3eye_dev *d, FILE *out);

/* Report whether the UAC interfaces (1 = audio control, 2 = audio streaming)
 * are addressable through the current WinUSB binding. Read-only: only queries
 * handles and descriptors, never issues SET_INTERFACE or SET_CONFIGURATION. */
void ps3eye_probe_audio(ps3eye_dev *d, FILE *out);

/* ---- Pipelined overlapped bulk reader -------------------------------- */

typedef struct ps3eye_reader ps3eye_reader;

ps3eye_reader *reader_create(ps3eye_dev *d, int slots, uint32_t buf_size);
void           reader_destroy(ps3eye_reader *r);

/* Submits all slots. */
bool reader_start(ps3eye_reader *r, char *err, size_t errlen);
/* Cancels outstanding transfers and returns every slot to idle. */
void reader_stop(ps3eye_reader *r);

/* Wait for one completed transfer.
 *  >0  a transfer completed; *buf / *len are valid until reader_release()
 *   0  timeout
 *  <0  fatal error (-1 io, -2 no slots armed, -3 aborted) */
int  reader_wait(ps3eye_reader *r, uint8_t **buf, uint32_t *len,
                 uint32_t timeout_ms);
/* Re-queue the slot handed out by the last successful reader_wait(). */
void reader_release(ps3eye_reader *r);

#endif /* PS3EYE_USB_H */
