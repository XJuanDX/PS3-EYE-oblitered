/* audio_main.c — ps3eye-audio
 *
 * Experimental UAC1 capture from the PlayStation Eye microphone over WinUSB.
 *
 * This exists to answer one question with measurement rather than inference:
 * can the microphone stream be read in user mode through the WinUSB binding
 * that is already installed, without any driver change?
 *
 * It uses the isochronous subset of WinUSB. The audio streaming interface is
 * switched to alternate setting 1 for the duration of the capture and always
 * restored to alternate setting 0 afterwards.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winusb.h>
#include <usb.h>            /* USBD_ISO_PACKET_DESCRIPTOR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ov534.h"
#include "ps3eye_usb.h"

#define DEFAULT_PACKETS 2048u
#define CAPTURE_TIMEOUT_MS 5000u

typedef enum { MODE_ASAP = 0, MODE_FRAME, MODE_FRAME_CONT } read_mode;

static void usage(void)
{
    printf("usage: ps3eye-audio [options]\n"
           "\n"
           "  --packets N       isochronous packets to request (default %u)\n"
           "  --out FILE        write the captured PCM payload to FILE (raw, no header)\n"
           "  --after-video     start the video stream first and keep it running\n"
           "  --mode M          read mode: asap (default), frame, frame-cont\n"
           "  --reset-pipe      call WinUsb_ResetPipe before the read\n"
           "  --flush-pipe      call WinUsb_FlushPipe before the read\n"
           "  --pipe-policy P   set pipe policy: auto-clear-stall, transfer-timeout=MS\n"
           "  --query-pipe-ex   dump WINUSB_PIPE_INFORMATION_EX\n"
           "  --help\n"
           "\n"
           "One packet is one USB service interval (a microframe on a\n"
           "high-speed bus), so N packets cover N/8000 s of bus time.\n",
           (unsigned)DEFAULT_PACKETS);
}

static double now_s(void)
{
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
    ps3eye_dev                dev;
    char                      err[512];
    WINUSB_INTERFACE_HANDLE   audio = NULL;
    USB_INTERFACE_DESCRIPTOR  ifd;
    WINUSB_PIPE_INFORMATION   pipe;
    WINUSB_ISOCH_BUFFER_HANDLE hbuf = NULL;
    USBD_ISO_PACKET_DESCRIPTOR *pd = NULL;
    OVERLAPPED                ov;
    HANDLE                    ev = NULL;
    uint8_t                  *buf = NULL;
    uint8_t                  *pcm = NULL;
    const char               *out_path = NULL;
    unsigned                  npackets = DEFAULT_PACKETS;
    bool                      alt1 = false, registered = false;
    bool                      restored = false;
    bool                      ok = false;
    bool                      after_video = false;
    bool                      do_reset = false, do_flush = false;
    bool                      do_query_ex = false;
    unsigned                  transfer_timeout = 0;
    bool                      auto_clear = false;
    read_mode                 mode = MODE_ASAP;
    ov534                     cam;
    ps3eye_reader            *rd = NULL;
    bool                      video_up = false;
    int                       i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(a, "--packets") == 0 && v) {
            npackets = (unsigned)atoi(v);
            i++;
        } else if (strcmp(a, "--out") == 0 && v) {
            out_path = v;
            i++;
        } else if (strcmp(a, "--after-video") == 0) {
            after_video = true;
        } else if (strcmp(a, "--mode") == 0 && v) {
            if (strcmp(v, "asap") == 0) mode = MODE_ASAP;
            else if (strcmp(v, "frame") == 0) mode = MODE_FRAME;
            else if (strcmp(v, "frame-cont") == 0) mode = MODE_FRAME_CONT;
            else { fprintf(stderr, "unknown --mode %s\n", v); return 2; }
            i++;
        } else if (strcmp(a, "--reset-pipe") == 0) {
            do_reset = true;
        } else if (strcmp(a, "--flush-pipe") == 0) {
            do_flush = true;
        } else if (strcmp(a, "--pipe-policy") == 0 && v) {
            if (strcmp(v, "auto-clear-stall") == 0) auto_clear = true;
            else if (strncmp(v, "transfer-timeout=", 17) == 0) {
                transfer_timeout = (unsigned)atoi(v + 17);
            } else {
                fprintf(stderr, "unknown --pipe-policy %s\n", v);
                return 2;
            }
            i++;
        } else if (strcmp(a, "--query-pipe-ex") == 0) {
            do_query_ex = true;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            usage();
            return 2;
        }
    }

    if (npackets == 0 || npackets > 16384u) {
        fprintf(stderr, "--packets must be between 1 and 16384\n");
        return 2;
    }

    if (!ps3eye_open(&dev, err, sizeof(err))) {
        fprintf(stderr, "cannot open PS3 Eye: %s\n", err);
        return 1;
    }

    if (after_video) {
        if (!ov534_init(&cam, &dev, err, sizeof(err))) {
            fprintf(stderr, "video init failed: %s\n", err);
            goto cleanup;
        }
        video_up = true;
        if (!ov534_start(&cam, PS3EYE_MODE_VGA_640x480, 30, err, sizeof(err))) {
            fprintf(stderr, "video start failed: %s\n", err);
            goto cleanup;
        }
        rd = reader_create(&dev, 4, 65536);
        if (!rd || !reader_start(rd, err, sizeof(err))) {
            fprintf(stderr, "video reader failed: %s\n", err);
            goto cleanup;
        }
        printf("video         : streaming 640x480, draining bulk IN\n");
    }

    /* The UAC streaming interface is the one WinUSB hands out as associated
     * interface index 1 on this device; see docs/AUDIO.md for the evidence. */
    if (!WinUsb_GetAssociatedInterface(dev.iface, 1, &audio)) {
        fprintf(stderr, "cannot reach the audio streaming interface: error %lu\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }

    memset(&ifd, 0, sizeof(ifd));
    if (!WinUsb_QueryInterfaceSettings(audio, 1, &ifd)) {
        fprintf(stderr, "cannot read alt 1 descriptor: error %lu\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }

    memset(&pipe, 0, sizeof(pipe));
    if (!WinUsb_QueryPipe(audio, 1, 0, &pipe)) {
        fprintf(stderr, "no pipe 0 on alt 1: error %lu\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }
    if (pipe.PipeType != UsbdPipeTypeIsochronous) {
        fprintf(stderr, "pipe 0x%02x is not isochronous (type %d)\n",
                (unsigned)pipe.PipeId, (int)pipe.PipeType);
        goto cleanup;
    }

    if (do_query_ex) {
        WINUSB_PIPE_INFORMATION_EX pipe_ex;
        memset(&pipe_ex, 0, sizeof(pipe_ex));
        if (WinUsb_QueryPipeEx(audio, 1, 0, &pipe_ex)) {
            printf("pipe ex       : max bytes/interval %u\n",
                   (unsigned)pipe_ex.MaximumBytesPerInterval);
        } else {
            printf("pipe ex       : WinUsb_QueryPipeEx failed (error %lu)\n",
                   (unsigned long)GetLastError());
        }
    }

    if (!WinUsb_SetCurrentAlternateSetting(audio, 1)) {
        fprintf(stderr, "SET_INTERFACE alt 1 failed: error %lu\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }
    alt1 = true;

    /* Optional pipe reset/flush before first read */
    if (do_reset && WinUsb_ResetPipe) {
        if (WinUsb_ResetPipe(audio, pipe.PipeId))
            printf("reset pipe    : ok\n");
        else
            printf("reset pipe    : failed (error %lu)\n", (unsigned long)GetLastError());
    }
    if (do_flush && WinUsb_FlushPipe) {
        if (WinUsb_FlushPipe(audio, pipe.PipeId))
            printf("flush pipe    : ok\n");
        else
            printf("flush pipe    : failed (error %lu)\n", (unsigned long)GetLastError());
    }

    /* Optional pipe policies */
    if (auto_clear) {
        ULONG val = 1;
        if (WinUsb_SetPipePolicy(audio, pipe.PipeId, 3 /* AUTO_CLEAR_STALL */, sizeof(val), &val))
            printf("pipe policy   : AUTO_CLEAR_STALL = 1\n");
        else
            printf("pipe policy   : AUTO_CLEAR_STALL failed (error %lu)\n", (unsigned long)GetLastError());
    }
    if (transfer_timeout) {
        ULONG val = transfer_timeout;
        if (WinUsb_SetPipePolicy(audio, pipe.PipeId, 4 /* PIPE_TRANSFER_TIMEOUT */, sizeof(val), &val))
            printf("pipe policy   : PIPE_TRANSFER_TIMEOUT = %u ms\n", transfer_timeout);
        else
            printf("pipe policy   : PIPE_TRANSFER_TIMEOUT failed (error %lu)\n", (unsigned long)GetLastError());
    }

    {
        /* UAC1 SET_CUR SAMPLING_FREQ_CONTROL: bmRequestType 0x22 (class,
         * endpoint, host-to-device), bRequest 0x01, wValue 0x0100,
         * wIndex = endpoint address, three-byte little-endian frequency.
         * The CS_ENDPOINT descriptor advertises this control
         * (bmAttributes 0x01) and host audio drivers issue it before the
         * first read, so it is part of a faithful bring-up. */
        uint8_t  hz[3];
        uint32_t moved = 0;

        hz[0] = (uint8_t)(16000 & 0xff);
        hz[1] = (uint8_t)((16000 >> 8) & 0xff);
        hz[2] = 0;

        if (ps3eye_ctrl_raw(&dev, 0x22, 0x01, 0x0100, pipe.PipeId,
                            hz, sizeof(hz), &moved)) {
            printf("sample rate   : SET_CUR 16000 Hz accepted (%lu byte(s))\n",
                   (unsigned long)moved);
        } else {
            printf("sample rate   : SET_CUR 16000 Hz failed, error %lu\n",
                   (unsigned long)GetLastError());
        }
    }

    printf("device        : PS3 Eye VID_%04x&PID_%04x via WinUSB\n",
           PS3EYE_VID, PS3EYE_PID);
    printf("iso pipe      : EP 0x%02x, max packet %u, interval %u\n",
           (unsigned)pipe.PipeId, (unsigned)pipe.MaximumPacketSize,
           (unsigned)pipe.Interval);
    printf("packets       : %u (%.3f ms of bus time)\n",
           npackets, (double)npackets / 8.0);
    printf("read mode     : %s\n",
           mode == MODE_ASAP ? "asap" : (mode == MODE_FRAME ? "frame" : "frame-cont"));

    buf = (uint8_t *)malloc((size_t)pipe.MaximumPacketSize * npackets);
    pd  = (USBD_ISO_PACKET_DESCRIPTOR *)calloc(npackets, sizeof(*pd));
    ev  = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!buf || !pd || !ev) {
        fprintf(stderr, "out of memory\n");
        goto cleanup;
    }

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;

    if (!WinUsb_RegisterIsochBuffer(audio, pipe.PipeId, buf,
                                    pipe.MaximumPacketSize * npackets, &hbuf)) {
        fprintf(stderr, "WinUsb_RegisterIsochBuffer failed: error %lu\n",
                (unsigned long)GetLastError());
        goto cleanup;
    }
    registered = true;

    {
        double t0 = now_s();
        DWORD  waited;
        DWORD  got = 0;
        ULONG  total = 0, nonempty = 0, minlen = 0xffffffffu, maxlen = 0;
        ULONG  errors = 0, k;
        ULONG  frame_number = 0;
        LARGE_INTEGER timestamp = {0};

        /* For frame-number modes, get a starting frame number */
        if (mode != MODE_ASAP) {
            if (!WinUsb_GetCurrentFrameNumber(audio, &frame_number, &timestamp)) {
                fprintf(stderr, "GetCurrentFrameNumber failed: error %lu\n",
                        (unsigned long)GetLastError());
                goto cleanup;
            }
            /* Schedule a few frames ahead to give the HC time to schedule */
            frame_number += 8;
            printf("start frame   : %lu\n", frame_number);
        }

        bool read_ok = false;

        if (mode == MODE_ASAP) {
            if (!WinUsb_ReadIsochPipeAsap(hbuf, 0, pipe.MaximumPacketSize * npackets,
                                          FALSE, npackets, pd, &ov)) {
                DWORD e = GetLastError();
                if (e != ERROR_IO_PENDING) {
                    fprintf(stderr, "WinUsb_ReadIsochPipeAsap failed: error %lu\n",
                            (unsigned long)e);
                    goto cleanup;
                }
            }
            read_ok = true;
        } else if (mode == MODE_FRAME) {
            if (!WinUsb_ReadIsochPipe(hbuf, 0, pipe.MaximumPacketSize * npackets,
                                      &frame_number, npackets, pd, &ov)) {
                DWORD e = GetLastError();
                if (e != ERROR_IO_PENDING) {
                    fprintf(stderr, "WinUsb_ReadIsochPipe failed: error %lu\n",
                            (unsigned long)e);
                    goto cleanup;
                }
            }
            read_ok = true;
        } else if (mode == MODE_FRAME_CONT) {
            /* Continuous: submit multiple reads back-to-back with adjusted frames */
            ULONG remaining = npackets;
            ULONG submitted = 0;
            ULONG fn = frame_number;
            LARGE_INTEGER ts = timestamp;

            while (remaining > 0) {
                ULONG this_batch = (remaining > 256) ? 256 : remaining;
                if (!WinUsb_ReadIsochPipe(hbuf, 0, pipe.MaximumPacketSize * this_batch,
                                          &fn, this_batch, pd + submitted, &ov)) {
                    DWORD e = GetLastError();
                    if (e != ERROR_IO_PENDING) {
                        fprintf(stderr, "WinUsb_ReadIsochPipe (cont) failed: error %lu\n",
                                (unsigned long)e);
                        goto cleanup;
                    }
                }

                waited = WaitForSingleObject(ev, CAPTURE_TIMEOUT_MS);
                if (waited == WAIT_TIMEOUT) {
                    fprintf(stderr, "iso transfer timed out after %u ms\n",
                            (unsigned)CAPTURE_TIMEOUT_MS);
                    WinUsb_AbortPipe(audio, pipe.PipeId);
                    break;
                }
                if (!WinUsb_GetOverlappedResult(audio, &ov, &got, FALSE)) {
                    fprintf(stderr, "GetOverlappedResult failed: error %lu\n",
                            (unsigned long)GetLastError());
                    goto cleanup;
                }

                submitted += this_batch;
                remaining -= this_batch;

                /* Adjust next frame number based on completion timestamp */
                if (!WinUsb_GetAdjustedFrameNumber(&fn, ts))
                    fn += this_batch;

                if (remaining > 0) {
                    ResetEvent(ev);
                    ov.hEvent = ev;
                }
            }
            read_ok = true;
            npackets = submitted; /* adjust for reporting */
        }

        if (!read_ok) {
            fprintf(stderr, "no read submitted\n");
            goto cleanup;
        }

        waited = WaitForSingleObject(ev, CAPTURE_TIMEOUT_MS);
        if (waited == WAIT_TIMEOUT) {
            fprintf(stderr, "iso transfer timed out after %u ms, aborting pipe\n",
                    (unsigned)CAPTURE_TIMEOUT_MS);
            WinUsb_AbortPipe(audio, pipe.PipeId);
        }

        if (!WinUsb_GetOverlappedResult(audio, &ov, &got, FALSE)) {
            fprintf(stderr, "WinUsb_GetOverlappedResult failed: error %lu\n",
                    (unsigned long)GetLastError());
            goto cleanup;
        }

        printf("transfer      : %.3f s wall clock, %lu bytes reported\n",
               now_s() - t0, (unsigned long)got);

        pcm = (uint8_t *)malloc((size_t)pipe.MaximumPacketSize * npackets);
        if (!pcm) {
            fprintf(stderr, "out of memory\n");
            goto cleanup;
        }

        {
            ULONG used = 0;

            for (k = 0; k < npackets; k++) {
                ULONG len = pd[k].Length;

                if (pd[k].Status != 0)
                    errors++;
                if (len == 0)
                    continue;
                nonempty++;
                if (len < minlen) minlen = len;
                if (len > maxlen) maxlen = len;
                if (pd[k].Offset + len <= pipe.MaximumPacketSize * npackets && used + len <= pipe.MaximumPacketSize * npackets) {
                    memcpy(pcm + used, buf + pd[k].Offset, len);
                    used += len;
                }
            }
            total = used;

            printf("packets in    : %lu non-empty of %u (%.1f%%), %lu status error(s)\n",
                   (unsigned long)nonempty, npackets,
                   100.0 * (double)nonempty / (double)npackets,
                   (unsigned long)errors);
            if (nonempty)
                printf("packet size   : min %lu, max %lu, total payload %lu bytes\n",
                       (unsigned long)minlen, (unsigned long)maxlen,
                       (unsigned long)total);

            if (nonempty) {
                double span_s = (double)npackets / 8000.0;
                printf("derived rate  : %.0f bytes/s over %.3f ms of bus time\n",
                       (double)total / span_s, span_s * 1000.0);
            }
        }

        if (total >= 8) {
            unsigned ch, nch = 4;
            unsigned frames = (unsigned)(total / (2u * nch));

            printf("interleaved   : assuming %u ch x 16 bit, %u sample frames "
                   "(%.1f ms at 16 kHz)\n", nch, frames,
                   (double)frames * 1000.0 / 16000.0);

            for (ch = 0; ch < nch; ch++) {
                double sum = 0.0, peak = 0.0;
                unsigned n = 0, zeros = 0, f;

                for (f = 0; f < frames; f++) {
                    size_t off = ((size_t)f * nch + ch) * 2u;
                    int16_t s = (int16_t)((uint16_t)pcm[off] |
                                          ((uint16_t)pcm[off + 1] << 8));
                    double  v = (double)s;

                    if (s == 0)
                        zeros++;
                    sum += v * v;
                    if (fabs(v) > peak)
                        peak = fabs(v);
                    n++;
                }
                if (n) {
                    printf("  channel %u   : rms %7.1f, peak %7.0f, zero %5.1f%%\n",
                           ch, sqrt(sum / (double)n), peak,
                           100.0 * (double)zeros / (double)n);
                }
            }
        }

        if (out_path) {
            FILE *f = fopen(out_path, "wb");

            if (!f) {
                fprintf(stderr, "cannot write %s\n", out_path);
            } else {
                if (total)
                    fwrite(pcm, 1, total, f);
                fclose(f);
                printf("pcm written   : %s (%lu bytes, no header)\n",
                       out_path, (unsigned long)total);
            }
        }

        ok = total > 0;
    }

cleanup:
    if (registered) {
        WinUsb_UnregisterIsochBuffer(hbuf);
        hbuf = NULL;
    }
    if (audio) {
        if (alt1) {
            SetLastError(0);
            restored = WinUsb_SetCurrentAlternateSetting(audio, 0) != FALSE;
            printf("alt setting   : restored to 0: %s\n",
                   restored ? "yes" : "FAILED");
        }
        WinUsb_Free(audio);
    }
    if (rd) {
        reader_stop(rd);
        reader_destroy(rd);
    }
    if (video_up)
        ov534_stop(&cam);
    if (ev)
        CloseHandle(ev);
    free(buf);
    free(pd);
    free(pcm);
    ps3eye_close(&dev);

    return ok && restored ? 0 : 1;
}