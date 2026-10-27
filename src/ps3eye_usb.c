/* ps3eye_usb.c — WinUSB transport for the PlayStation Eye (OV534 bridge).
 *
 * Written from scratch against the documented WinUSB API.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <stdlib.h>
#include <string.h>

#include "ps3eye_usb.h"

const GUID PS3EYE_IFACE_GUID = {
    0x4BCC4C51, 0x4249, 0x4AE8,
    { 0x98, 0xBF, 0x35, 0x6B, 0x2C, 0x53, 0x0E, 0x77 }
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

void ps3eye_format_error(char *buf, size_t len, const char *what)
{
    DWORD  code = GetLastError();
    char  *msg  = NULL;

    if (len == 0)
        return;

    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, 0, (LPSTR)&msg, 0, NULL) && msg) {
        size_t n = strlen(msg);
        while (n > 0 && (msg[n - 1] == '\r' || msg[n - 1] == '\n'))
            msg[--n] = '\0';
        _snprintf_s(buf, len, _TRUNCATE, "%s: %s (0x%08lx)", what, msg, (unsigned long)code);
        LocalFree(msg);
    } else {
        _snprintf_s(buf, len, _TRUNCATE, "%s: error 0x%08lx", what, (unsigned long)code);
    }
}

static bool contains_ci(const char *hay, const char *needle)
{
    size_t nh = strlen(hay), nn = strlen(needle), i, j;

    if (nn == 0 || nn > nh)
        return false;

    for (i = 0; i + nn <= nh; i++) {
        for (j = 0; j < nn; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b)
                break;
        }
        if (j == nn)
            return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* open / close                                                        */
/* ------------------------------------------------------------------ */

static bool bind_interface(ps3eye_dev *d, char *err, size_t errlen)
{
    USB_INTERFACE_DESCRIPTOR ifd;
    UCHAR i;

    memset(&ifd, 0, sizeof(ifd));
    if (!WinUsb_QueryInterfaceSettings(d->iface, 0, &ifd)) {
        ps3eye_format_error(err, errlen, "WinUsb_QueryInterfaceSettings");
        return false;
    }

    d->bulk_in_pipe = 0;
    d->bulk_in_max_packet = 0;

    for (i = 0; i < ifd.bNumEndpoints; i++) {
        WINUSB_PIPE_INFORMATION pipe;
        memset(&pipe, 0, sizeof(pipe));
        if (!WinUsb_QueryPipe(d->iface, 0, i, &pipe))
            continue;
        if (pipe.PipeType == UsbdPipeTypeBulk && USB_ENDPOINT_DIRECTION_IN(pipe.PipeId)) {
            d->bulk_in_pipe = pipe.PipeId;
            d->bulk_in_max_packet = pipe.MaximumPacketSize;
            break;
        }
    }

    if (d->bulk_in_pipe == 0) {
        _snprintf_s(err, errlen, _TRUNCATE,
                    "no bulk IN pipe on interface 0 alt 0 (interface class %02x/%02x)",
                    ifd.bInterfaceClass, ifd.bInterfaceSubClass);
        return false;
    }
    return true;
}

bool ps3eye_open(ps3eye_dev *d, char *err, size_t errlen)
{
    HDEVINFO                      set;
    SP_DEVICE_INTERFACE_DATA      did;
    DWORD                         index;
    bool                          ok = false;
    ULONG                         len;
    UCHAR                         speed = 0;

    memset(d, 0, sizeof(*d));
    d->file  = INVALID_HANDLE_VALUE;
    d->iface = NULL;

    if (errlen)
        err[0] = '\0';

    set = SetupDiGetClassDevsA(&PS3EYE_IFACE_GUID, NULL, NULL,
                               DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        ps3eye_format_error(err, errlen, "SetupDiGetClassDevs");
        return false;
    }

    for (index = 0; ; index++) {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_A detail = NULL;
        DWORD                             needed = 0;
        HANDLE                            file;

        memset(&did, 0, sizeof(did));
        did.cbSize = sizeof(did);

        if (!SetupDiEnumDeviceInterfaces(set, NULL, &PS3EYE_IFACE_GUID, index, &did)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                ps3eye_format_error(err, errlen, "no PS3 Eye found (device interface GUID)");
            else
                ps3eye_format_error(err, errlen, "SetupDiEnumDeviceInterfaces");
            break;
        }

        SetupDiGetDeviceInterfaceDetailA(set, &did, NULL, 0, &needed, NULL);
        if (needed == 0) {
            ps3eye_format_error(err, errlen, "SetupDiGetDeviceInterfaceDetail(size)");
            break;
        }
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(needed);
        if (!detail) {
            _snprintf_s(err, errlen, _TRUNCATE, "out of memory");
            break;
        }
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (!SetupDiGetDeviceInterfaceDetailA(set, &did, detail, needed, NULL, NULL)) {
            ps3eye_format_error(err, errlen, "SetupDiGetDeviceInterfaceDetail");
            free(detail);
            continue;
        }

        /* The interface GUID is shared with any other libwdi-bound device,
         * so filter by VID/PID. */
        if (!contains_ci(detail->DevicePath, "vid_1415&pid_2000")) {
            free(detail);
            continue;
        }

        file = CreateFileA(detail->DevicePath,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            ps3eye_format_error(err, errlen, "CreateFile(device interface)");
            free(detail);
            break;
        }

        d->file = file;
        if (!WinUsb_Initialize(file, &d->iface)) {
            ps3eye_format_error(err, errlen, "WinUsb_Initialize");
            CloseHandle(file);
            d->file = INVALID_HANDLE_VALUE;
            free(detail);
            break;
        }

        len = sizeof(speed);
        if (!WinUsb_QueryDeviceInformation(d->iface, DEVICE_SPEED, &len, &speed))
            speed = 0;
        d->bus_speed = speed;

        if (!bind_interface(d, err, errlen)) {
            WinUsb_Free(d->iface);
            CloseHandle(file);
            d->iface = NULL;
            d->file  = INVALID_HANDLE_VALUE;
            free(detail);
            break;
        }

        d->opened = true;
        ok = true;
        free(detail);
        break;
    }

    SetupDiDestroyDeviceInfoList(set);
    return ok;
}

void ps3eye_close(ps3eye_dev *d)
{
    if (d->iface) {
        WinUsb_Free(d->iface);
        d->iface = NULL;
    }
    if (d->file != INVALID_HANDLE_VALUE && d->file != NULL) {
        CloseHandle(d->file);
        d->file = INVALID_HANDLE_VALUE;
    }
    d->opened = false;
}

/* ------------------------------------------------------------------ */
/* control transfers                                                   */
/* ------------------------------------------------------------------ */

static bool do_control(ps3eye_dev *d, uint8_t direction, uint8_t request,
                       uint16_t value, uint16_t index, void *data, uint32_t len,
                       uint32_t *transferred)
{
    WINUSB_SETUP_PACKET setup;
    ULONG               got = 0;

    memset(&setup, 0, sizeof(setup));
    setup.RequestType = (UCHAR)(0x40 | (direction ? 0x80 : 0x00)); /* vendor, device */
    setup.Request     = request;
    setup.Value       = value;
    setup.Index       = index;
    setup.Length      = (USHORT)len;

    if (!WinUsb_ControlTransfer(d->iface, setup, (PUCHAR)data, len, &got, NULL))
        return false;

    if (transferred)
        *transferred = got;
    return true;
}

bool ps3eye_ctrl_out(ps3eye_dev *d, uint8_t request, uint16_t value,
                     uint16_t index, const void *data, uint32_t len)
{
    return do_control(d, 0, request, value, index, (void *)data, len, NULL);
}

bool ps3eye_ctrl_in(ps3eye_dev *d, uint8_t request, uint16_t value,
                    uint16_t index, void *data, uint32_t len,
                    uint32_t *transferred)
{
    return do_control(d, 1, request, value, index, data, len, transferred);
}

/* ------------------------------------------------------------------ */
/* descriptor dump                                                     */
/* ------------------------------------------------------------------ */

static const char *pipe_type_name(uint8_t attr)
{
    switch (attr & 0x03) {
    case 0:  return "control";
    case 1:  return "isochronous";
    case 2:  return "bulk";
    default: return "interrupt";
    }
}

static const char *speed_name(uint8_t s)
{
    switch (s) {
    case 1:  return "low (1.5 Mbit/s)";
    case 2:  return "full (12 Mbit/s)";
    case 3:  return "high (480 Mbit/s)";
    case 4:  return "super (5 Gbit/s)";
    default: return "unknown";
    }
}

static const char *terminal_type_name(uint16_t t)
{
    switch (t) {
    case 0x0100: return "USB undefined";
    case 0x0101: return "USB streaming";
    case 0x0200: return "input undefined";
    case 0x0201: return "microphone";
    case 0x0202: return "desktop microphone";
    case 0x0203: return "personal computer";
    case 0x0204: return "microphone array";
    case 0x0300: return "output undefined";
    case 0x0301: return "speaker";
    case 0x0302: return "headphones";
    case 0x0600: return "external undefined";
    case 0x0601: return "analog connector";
    case 0x0602: return "digital audio interface";
    case 0x0603: return "line connector";
    case 0x0604: return "legacy audio connector";
    case 0x0605: return "S/PDIF interface";
    default:     return "other";
    }
}

/* Decode the UAC1 class-specific descriptors: the only way to learn what the
 * audio function actually offers without streaming it. */
static void dump_uac(uint8_t *raw, uint32_t got, FILE *out)
{
    uint8_t  cur_class = 0, cur_sub = 0, cur_alt = 0;
    uint32_t off = 0;
    bool     any = false;

    while (off + 2 <= got) {
        uint8_t blen  = raw[off];
        uint8_t btype = raw[off + 1];

        if (blen < 2 || off + blen > got)
            break;

        if (btype == 0x04 && blen >= 9) {
            cur_class = raw[off + 5];
            cur_sub   = raw[off + 6];
            cur_alt   = raw[off + 3];
        } else if (btype == 0x24 && blen >= 4 &&
                   cur_class == 0x01 && cur_sub == 0x01) {
            uint8_t s = raw[off + 2];
            if (!any) {
                fprintf(out, "\n  audio control function (UAC1):\n");
                any = true;
            }
            if (s == 0x01 && blen >= 9) {
                fprintf(out, "    AC header: bcdADC 0x%02x%02x, %u streaming "
                             "interface(s)\n",
                        raw[off + 4], raw[off + 3], (unsigned)raw[off + 7]);
            } else if (s == 0x02 && blen >= 12) {
                uint16_t tt = (uint16_t)(raw[off + 4] | (raw[off + 5] << 8));
                fprintf(out, "    INPUT_TERMINAL id %u: type 0x%04x (%s), "
                             "%u channel(s), ch_config 0x%02x%02x\n",
                        (unsigned)raw[off + 3], (unsigned)tt,
                        terminal_type_name(tt), (unsigned)raw[off + 7],
                        raw[off + 9], raw[off + 8]);
            } else if (s == 0x03 && blen >= 9) {
                uint16_t tt = (uint16_t)(raw[off + 4] | (raw[off + 5] << 8));
                fprintf(out, "    OUTPUT_TERMINAL id %u: type 0x%04x (%s), "
                             "source unit %u\n",
                        (unsigned)raw[off + 3], (unsigned)tt,
                        terminal_type_name(tt), (unsigned)raw[off + 7]);
            } else if (s == 0x06 && blen >= 7) {
                uint8_t  csize = raw[off + 5];
                unsigned nbm   = csize ? (unsigned)(blen - 7) / csize : 0;
                unsigned k;

                fprintf(out, "    FEATURE_UNIT id %u: source %u, "
                             "control size %u, %u bitmap(s):",
                        (unsigned)raw[off + 3], (unsigned)raw[off + 4],
                        (unsigned)csize, nbm);
                for (k = 0; k < nbm; k++) {
                    unsigned b;
                    fprintf(out, " [%u]", k);
                    for (b = 0; b < csize; b++)
                        fprintf(out, "%02x", raw[off + 6 + k * csize + b]);
                }
                fprintf(out, "\n");
            }
        } else if (btype == 0x24 && blen >= 4 &&
                   cur_class == 0x01 && cur_sub == 0x02) {
            uint8_t s = raw[off + 2];
            if (s == 0x01 && blen >= 7) {
                fprintf(out, "    AS_GENERAL alt %u: terminal link %u, "
                             "format tag 0x%02x%02x\n",
                        (unsigned)cur_alt, (unsigned)raw[off + 3],
                        raw[off + 6], raw[off + 5]);
            } else if (s == 0x02 && blen >= 8) {
                uint8_t  nch    = raw[off + 4];
                uint8_t  sub    = raw[off + 5];
                uint8_t  bits   = raw[off + 6];
                uint8_t  nrates = raw[off + 7];
                uint32_t f;
                fprintf(out, "    AS_FORMAT_TYPE_I alt %u: %u channel(s), "
                             "%u bytes/sample, %u bit, %u rate(s):",
                        (unsigned)cur_alt, (unsigned)nch, (unsigned)sub,
                        (unsigned)bits, (unsigned)nrates);
                for (f = 0; f < nrates && (8 + (f + 1) * 3) <= blen; f++) {
                    uint32_t hz = (uint32_t)raw[off + 8 + f * 3] |
                                  ((uint32_t)raw[off + 9 + f * 3] << 8) |
                                  ((uint32_t)raw[off + 10 + f * 3] << 16);
                    fprintf(out, " %u Hz", (unsigned)hz);
                }
                fprintf(out, "\n");
                if (nch && sub && nrates) {
                    uint32_t hz = (uint32_t)raw[off + 8] |
                                  ((uint32_t)raw[off + 9] << 8) |
                                  ((uint32_t)raw[off + 10] << 16);
                    fprintf(out, "      uncompressed rate: %u bytes/s\n",
                            (unsigned)(hz * nch * sub));
                }
            }
        } else if (btype == 0x25 && blen >= 5) {
            fprintf(out, "    CS_ENDPOINT: bmAttributes 0x%02x\n",
                    (unsigned)raw[off + 3]);
        }
        off += blen;
    }
}

void ps3eye_dump_descriptors(ps3eye_dev *d, FILE *out)
{
    USB_CONFIGURATION_DESCRIPTOR cfg;
    ULONG                        got = 0;
    uint8_t                     *raw;
    uint16_t                     total;
    uint32_t                     off;

    fprintf(out, "bus speed            : %s\n", speed_name(d->bus_speed));
    fprintf(out, "bulk IN pipe id      : 0x%02x (max packet %u)\n",
            d->bulk_in_pipe, (unsigned)d->bulk_in_max_packet);

    memset(&cfg, 0, sizeof(cfg));
    if (!WinUsb_GetDescriptor(d->iface, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
                              (PUCHAR)&cfg, sizeof(cfg), &got)) {
        fprintf(out, "WinUsb_GetDescriptor(header) failed: %lu\n", GetLastError());
        return;
    }

    total = cfg.wTotalLength;
    fprintf(out, "configuration        : %u, %u interface(s), wTotalLength %u, "
                 "bMaxPower %u mA\n",
            (unsigned)cfg.bConfigurationValue, (unsigned)cfg.bNumInterfaces,
            (unsigned)total, (unsigned)cfg.MaxPower * 2u);

    raw = (uint8_t *)malloc(total);
    if (!raw) {
        fprintf(out, "out of memory reading configuration descriptor\n");
        return;
    }
    if (!WinUsb_GetDescriptor(d->iface, USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0,
                              raw, total, &got)) {
        fprintf(out, "WinUsb_GetDescriptor(body) failed: %lu\n", GetLastError());
        free(raw);
        return;
    }

    fprintf(out, "\n  raw configuration descriptor (%lu bytes):\n", (unsigned long)got);
    for (off = 0; off < got; off += 16) {
        ULONG k;
        fprintf(out, "    %04x  ", (unsigned)off);
        for (k = 0; k < 16 && off + k < got; k++)
            fprintf(out, "%02x ", raw[off + k]);
        fprintf(out, "\n");
    }

    fprintf(out, "\n  parsed:\n");
    off = 0;
    while (off + 2 <= got) {
        uint8_t blen = raw[off];
        uint8_t btype = raw[off + 1];

        if (blen < 2 || off + blen > got) {
            fprintf(out, "    malformed descriptor at 0x%04x (bLength %u)\n",
                    (unsigned)off, (unsigned)blen);
            break;
        }

        switch (btype) {
        case 0x02:
            fprintf(out, "    CONFIGURATION %u: %u interface(s)\n",
                    (unsigned)raw[off + 5], (unsigned)raw[off + 4]);
            break;
        case 0x04:
            fprintf(out, "    INTERFACE %u alt %u: %u endpoint(s), "
                         "class %02x/%02x/%02x\n",
                    (unsigned)raw[off + 2], (unsigned)raw[off + 3],
                    (unsigned)raw[off + 4],
                    (unsigned)raw[off + 5], (unsigned)raw[off + 6],
                    (unsigned)raw[off + 7]);
            break;
        case 0x05:
            if (blen >= 7) {
                uint8_t  addr = raw[off + 2];
                uint8_t  attr = raw[off + 3];
                uint16_t mps  = (uint16_t)(raw[off + 4] | (raw[off + 5] << 8));
                fprintf(out, "      EP 0x%02x %s %-11s maxpacket %-5u interval %u\n",
                        (unsigned)addr, (addr & 0x80) ? "IN " : "OUT",
                        pipe_type_name(attr), (unsigned)mps,
                        (unsigned)raw[off + 6]);
            }
            break;
        case 0x0b:
            fprintf(out, "    INTERFACE ASSOCIATION (IAD) first_if %u count %u\n",
                    (unsigned)raw[off + 2], (unsigned)raw[off + 3]);
            break;
        default:
            break;
        }
        off += blen;
    }

    dump_uac(raw, got, out);
    free(raw);

    ps3eye_probe_audio(d, out);
}

void ps3eye_probe_audio(ps3eye_dev *d, FILE *out)
{
    USB_INTERFACE_DESCRIPTOR ifd;
    unsigned                 idx;

    fprintf(out, "\n  audio reachability through this WinUSB handle:\n");

    memset(&ifd, 0, sizeof(ifd));
    if (WinUsb_QueryInterfaceSettings(d->iface, 0, &ifd)) {
        fprintf(out, "    bound interface      : %u (class %02x/%02x/%02x)\n",
                (unsigned)ifd.bInterfaceNumber, (unsigned)ifd.bInterfaceClass,
                (unsigned)ifd.bInterfaceSubClass,
                (unsigned)ifd.bInterfaceProtocol);
    }

    /* Without an IAD, WinUSB is not expected to hand out the other
     * interfaces; the exact error code is the evidence we want. */
    for (idx = 1; idx <= 2; idx++) {
        WINUSB_INTERFACE_HANDLE a = NULL;
        DWORD                   err;

        SetLastError(0);
        if (!WinUsb_GetAssociatedInterface(d->iface, (UCHAR)idx, &a)) {
            err = GetLastError();
            fprintf(out, "    associated iface %u    : unavailable "
                         "(WinUsb_GetAssociatedInterface error %lu)\n",
                    idx, (unsigned long)err);
            continue;
        }

        memset(&ifd, 0, sizeof(ifd));
        if (WinUsb_QueryInterfaceSettings(a, 0, &ifd)) {
            fprintf(out, "    associated iface %u    : ok, bInterfaceNumber %u, "
                         "class %02x/%02x/%02x, %u endpoint(s) at alt 0\n",
                    idx, (unsigned)ifd.bInterfaceNumber,
                    (unsigned)ifd.bInterfaceClass,
                    (unsigned)ifd.bInterfaceSubClass,
                    (unsigned)ifd.bInterfaceProtocol,
                    (unsigned)ifd.bNumEndpoints);
        } else {
            fprintf(out, "    associated iface %u    : handle ok, "
                         "WinUsb_QueryInterfaceSettings failed (%lu)\n",
                    idx, (unsigned long)GetLastError());
        }
        WinUsb_Free(a);
    }
}

/* ------------------------------------------------------------------ */
/* pipelined bulk reader                                               */
/* ------------------------------------------------------------------ */

#define READER_MAX_SLOTS 16

typedef struct {
    OVERLAPPED ov;
    uint8_t   *buf;
    bool       pending;
    bool       done;
    uint32_t   len;
} reader_slot;

struct ps3eye_reader {
    ps3eye_dev *dev;
    int         nslots;
    uint32_t    buf_size;
    int         current;
    bool        failed;
    reader_slot slots[READER_MAX_SLOTS];
};

static bool submit_slot(ps3eye_reader *r, int i)
{
    reader_slot *s = &r->slots[i];
    DWORD        got = 0;

    ResetEvent(s->ov.hEvent);
    s->ov.Offset     = 0;
    s->ov.OffsetHigh = 0;
    s->pending = false;
    s->done    = false;
    s->len     = 0;

    if (WinUsb_ReadPipe(r->dev->iface, r->dev->bulk_in_pipe, s->buf,
                        r->buf_size, &got, &s->ov)) {
        s->len  = got;
        s->done = true;
        return true;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        s->pending = true;
        return true;
    }
    return false;
}

ps3eye_reader *reader_create(ps3eye_dev *d, int slots, uint32_t buf_size)
{
    ps3eye_reader *r;
    int            i;

    if (slots < 1)
        slots = 1;
    if (slots > READER_MAX_SLOTS)
        slots = READER_MAX_SLOTS;

    r = (ps3eye_reader *)calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->dev      = d;
    r->nslots   = slots;
    r->buf_size = buf_size;
    r->current  = -1;

    for (i = 0; i < slots; i++) {
        r->slots[i].ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        r->slots[i].buf       = (uint8_t *)malloc(buf_size);
        if (!r->slots[i].ov.hEvent || !r->slots[i].buf) {
            reader_destroy(r);
            return NULL;
        }
    }
    return r;
}

bool reader_start(ps3eye_reader *r, char *err, size_t errlen)
{
    int i;

    for (i = 0; i < r->nslots; i++) {
        if (!submit_slot(r, i)) {
            ps3eye_format_error(err, errlen, "WinUsb_ReadPipe");
            reader_stop(r);
            return false;
        }
    }
    r->failed = false;
    return true;
}

int reader_wait(ps3eye_reader *r, uint8_t **buf, uint32_t *len,
                uint32_t timeout_ms)
{
    ULONGLONG start = GetTickCount64();

    if (r->failed)
        return -1;

    for (;;) {
        HANDLE  events[READER_MAX_SLOTS];
        int     map[READER_MAX_SLOTS];
        int     n = 0;
        DWORD   waited;
        DWORD   got = 0;
        int     i, s;
        ULONGLONG elapsed;

        for (i = 0; i < r->nslots; i++) {
            if (r->slots[i].done) {
                r->current = i;
                *buf = r->slots[i].buf;
                *len = r->slots[i].len;
                return 1;
            }
        }

        for (i = 0; i < r->nslots; i++) {
            if (r->slots[i].pending) {
                events[n] = r->slots[i].ov.hEvent;
                map[n]    = i;
                n++;
            }
        }
        if (n == 0)
            return -2;

        elapsed = GetTickCount64() - start;
        if (elapsed >= timeout_ms)
            return 0;

        waited = WaitForMultipleObjects((DWORD)n, events, FALSE,
                                        (DWORD)(timeout_ms - elapsed));
        if (waited == WAIT_TIMEOUT)
            return 0;
        if (waited == WAIT_FAILED)
            return -1;

        s = map[waited - WAIT_OBJECT_0];
        if (!GetOverlappedResult(r->dev->file, &r->slots[s].ov, &got, FALSE)) {
            DWORD e = GetLastError();
            r->slots[s].pending = false;
            if (e == ERROR_OPERATION_ABORTED)
                return -3;
            r->failed = true;
            return -1;
        }
        r->slots[s].pending = false;
        r->slots[s].done    = true;
        r->slots[s].len     = got;
    }
}

void reader_release(ps3eye_reader *r)
{
    int i = r->current;

    if (i < 0)
        return;

    r->slots[i].done = false;
    r->current       = -1;

    if (!submit_slot(r, i))
        r->failed = true;
}

void reader_stop(ps3eye_reader *r)
{
    int i;

    for (i = 0; i < r->nslots; i++) {
        if (r->slots[i].pending)
            CancelIoEx(r->dev->file, &r->slots[i].ov);
    }
    for (i = 0; i < r->nslots; i++) {
        reader_slot *s = &r->slots[i];
        if (s->pending) {
            DWORD got = 0;
            GetOverlappedResult(r->dev->file, &s->ov, &got, TRUE);
        }
        s->pending = false;
        s->done    = false;
    }
    r->current = -1;
}

void reader_destroy(ps3eye_reader *r)
{
    int i;

    if (!r)
        return;

    reader_stop(r);

    for (i = 0; i < r->nslots; i++) {
        if (r->slots[i].ov.hEvent)
            CloseHandle(r->slots[i].ov.hEvent);
        free(r->slots[i].buf);
    }
    free(r);
}
