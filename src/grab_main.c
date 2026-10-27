/* grab_main.c — ps3eye-grab
 *
 * Grabs N frames from the PlayStation Eye over WinUSB, reassembles YUY2
 * frames and writes them to disk. Reports observed rates and error counters.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image_out.h"
#include "ov534.h"
#include "ps3eye_frame.h"
#include "ps3eye_usb.h"

#define READER_SLOTS 8
#define READER_BUF   65536u
#define WAIT_MS      500u
#define STALL_MS     4000u

typedef enum { OUT_PPM = 0, OUT_YUY2 = 1 } out_format;

typedef struct {
    unsigned    frames;
    unsigned    skip;
    unsigned    fps;
    const char *out;
    ps3eye_mode mode;
    out_format  format;
    bool        quiet;
    bool        info_only;
} config;

static volatile LONG g_stop = 0;

static BOOL WINAPI on_ctrl(DWORD type)
{
    (void)type;
    g_stop = 1;
    return TRUE;
}

static double now_s(void)
{
    LARGE_INTEGER f, c;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static void usage(void)
{
    printf(
        "usage: ps3eye-grab [options]\n"
        "\n"
        "  --frames N      frames to write (default 30)\n"
        "  --out DIR       output directory (default out)\n"
        "  --size WxH      640x480 (default) or 320x240\n"
        "  --fps N         requested frame rate (default 30)\n"
        "  --format F      ppm (default) or yuy2\n"
        "  --skip N        warm-up frames to drop before writing (default 5)\n"
        "  --info          print device info and exit\n"
        "  --quiet         no per-frame progress\n"
        "  --help\n");
}

static bool parse_size(const char *s, ps3eye_mode *m)
{
    if (strcmp(s, "640x480") == 0) {
        *m = PS3EYE_MODE_VGA_640x480;
        return true;
    }
    if (strcmp(s, "320x240") == 0) {
        *m = PS3EYE_MODE_QVGA_320x240;
        return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    config     cfg;
    char       err[512];
    ps3eye_dev dev;
    ov534      cam;
    double     t_start = 0.0, t_init = 0.0, t_first = 0.0, t_last = 0.0;
    unsigned   written = 0, skip_left;
    unsigned   consecutive_timeouts = 0;
    double     last_progress = 0.0;
    int        i;
    bool       ok = false;

    cfg.frames    = 30;
    cfg.skip      = 5;
    cfg.fps       = 30;
    cfg.out       = "out";
    cfg.mode      = PS3EYE_MODE_VGA_640x480;
    cfg.format    = OUT_PPM;
    cfg.quiet     = false;
    cfg.info_only = false;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (strcmp(a, "--frames") == 0 && v) {
            cfg.frames = (unsigned)atoi(v);
            i++;
        } else if (strcmp(a, "--skip") == 0 && v) {
            cfg.skip = (unsigned)atoi(v);
            i++;
        } else if (strcmp(a, "--fps") == 0 && v) {
            cfg.fps = (unsigned)atoi(v);
            i++;
        } else if (strcmp(a, "--out") == 0 && v) {
            cfg.out = v;
            i++;
        } else if (strcmp(a, "--size") == 0 && v) {
            if (!parse_size(v, &cfg.mode)) {
                fprintf(stderr, "unsupported --size %s\n", v);
                return 2;
            }
            i++;
        } else if (strcmp(a, "--format") == 0 && v) {
            if (strcmp(v, "ppm") == 0)
                cfg.format = OUT_PPM;
            else if (strcmp(v, "yuy2") == 0)
                cfg.format = OUT_YUY2;
            else {
                fprintf(stderr, "unsupported --format %s\n", v);
                return 2;
            }
            i++;
        } else if (strcmp(a, "--info") == 0) {
            cfg.info_only = true;
        } else if (strcmp(a, "--quiet") == 0) {
            cfg.quiet = true;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            usage();
            return 2;
        }
    }

    if (cfg.frames == 0) {
        fprintf(stderr, "--frames must be > 0\n");
        return 2;
    }

    if (!ps3eye_open(&dev, err, sizeof(err))) {
        fprintf(stderr, "cannot open PS3 Eye: %s\n", err);
        return 1;
    }

    printf("device        : PS3 Eye VID_%04x&PID_%04x via WinUSB\n",
           PS3EYE_VID, PS3EYE_PID);
    printf("bulk IN pipe  : 0x%02x (max packet %u)\n",
           dev.bulk_in_pipe, (unsigned)dev.bulk_in_max_packet);

    if (cfg.info_only) {
        ps3eye_dump_descriptors(&dev, stdout);
        ps3eye_close(&dev);
        return 0;
    }

    t_init = now_s();

    if (!ov534_init(&cam, &dev, err, sizeof(err))) {
        fprintf(stderr, "ov534_init failed: %s\n", err);
        ps3eye_close(&dev);
        return 1;
    }
    printf("sensor ID     : 0x%04x\n", cam.sensor_id);

    if (!ov534_start(&cam, cfg.mode, (uint8_t)cfg.fps, err, sizeof(err))) {
        fprintf(stderr, "ov534_start failed: %s\n", err);
        ov534_stop(&cam);
        ps3eye_close(&dev);
        return 1;
    }
    printf("mode          : %s YUY2, requested %u fps, sensor selected %u fps\n",
           ov534_mode_name(cfg.mode), cfg.fps, (unsigned)cam.fps);

    /* Streaming starts here; every rate below is measured from this point. */
    t_start = now_s();

    if (!ensure_dir(cfg.out)) {
        fprintf(stderr, "cannot create output directory: %s\n", cfg.out);
        ov534_stop(&cam);
        ps3eye_close(&dev);
        return 1;
    }

    {
        uint32_t      expected = ov534_frame_bytes(cfg.mode);
        frame_asm     asm_;
        ps3eye_reader *rd;
        double        last_frame_time = now_s();

        if (!fa_init(&asm_, expected)) {
            fprintf(stderr, "out of memory\n");
            ov534_stop(&cam);
            ps3eye_close(&dev);
            return 1;
        }
        fa_reset(&asm_);

        rd = reader_create(&dev, READER_SLOTS, READER_BUF);
        if (!rd) {
            fprintf(stderr, "cannot allocate reader\n");
            fa_free(&asm_);
            ov534_stop(&cam);
            ps3eye_close(&dev);
            return 1;
        }

        if (!reader_start(rd, err, sizeof(err))) {
            fprintf(stderr, "cannot start bulk reads: %s\n", err);
            reader_destroy(rd);
            fa_free(&asm_);
            ov534_stop(&cam);
            ps3eye_close(&dev);
            return 1;
        }

        SetConsoleCtrlHandler(on_ctrl, TRUE);

        skip_left = cfg.skip;

        while (written < cfg.frames && !g_stop) {
            uint8_t  *buf = NULL;
            uint32_t  len = 0;
            int       rc = reader_wait(rd, &buf, &len, WAIT_MS);

            if (rc == 1) {
                consecutive_timeouts = 0;

                if (fa_feed(&asm_, buf, len)) {
                    double t = now_s();
                    last_frame_time = t;

                    if (skip_left > 0) {
                        skip_left--;
                    } else {
                        char path[1024];

                        if (cfg.format == OUT_PPM) {
                            _snprintf_s(path, sizeof(path), _TRUNCATE,
                                        "%s/frame_%04u.ppm", cfg.out, written);

                            if (!write_ppm_yuy2(path, asm_.buf,
                                                ov534_width(cfg.mode),
                                                ov534_height(cfg.mode))) {
                                fprintf(stderr, "\ncannot write %s\n", path);
                                break;
                            }
                        } else {
                            _snprintf_s(path, sizeof(path), _TRUNCATE,
                                        "%s/frame_%04u.yuy2", cfg.out, written);
                            if (!write_raw(path, asm_.buf, expected)) {
                                fprintf(stderr, "\ncannot write %s\n", path);
                                break;
                            }
                        }

                        if (written == 0)
                            t_first = t;
                        t_last = t;
                        written++;

                        if (!cfg.quiet && (t - last_progress) > 0.25) {
                            last_progress = t;
                            printf("\r  writing frame %u/%u", written, cfg.frames);
                            fflush(stdout);
                        }
                    }
                }
                reader_release(rd);

                if ((now_s() - last_frame_time) * 1000.0 > STALL_MS) {
                    fprintf(stderr, "\nno complete frame for %u ms, aborting\n",
                            (unsigned)STALL_MS);
                    break;
                }
            } else if (rc == 0) {
                consecutive_timeouts++;
                if (consecutive_timeouts >= 3) {
                    fprintf(stderr, "\nno USB data for %u ms, aborting\n",
                            (unsigned)(WAIT_MS * 3));
                    break;
                }
                if ((now_s() - last_frame_time) * 1000.0 > STALL_MS) {
                    fprintf(stderr, "\nno complete frame for %u ms, aborting\n",
                            (unsigned)STALL_MS);
                    break;
                }
            } else {
                fprintf(stderr, "\nbulk read failed (code %d)\n", rc);
                break;
            }
        }

        SetConsoleCtrlHandler(on_ctrl, FALSE);
        if (!cfg.quiet)
            printf("\n");

        /* Order matters: stop the stream, then cancel I/O, then free. */
        ov534_stop(&cam);
        reader_stop(rd);

        {
            double elapsed = now_s() - t_start;
            double window  = (t_last > t_first) ? (t_last - t_first) : 0.0;
            double mb      = (double)asm_.stats.bytes / (1024.0 * 1024.0);

            printf("\n--- summary ---\n");
            printf("frames written      : %u\n", written);
            printf("frames reassembled  : %llu\n", (unsigned long long)asm_.stats.frames_ok);
            printf("frames dropped      : %llu\n", (unsigned long long)asm_.stats.frames_dropped);
            printf("chunks / bytes      : %llu / %llu\n",
                   (unsigned long long)asm_.stats.chunks,
                   (unsigned long long)asm_.stats.bytes);
            printf("init/start          : %.3f s\n", (t_start > t_init) ? (t_start - t_init) : 0.0);
            printf("capture elapsed     : %.3f s\n", elapsed);
            if (window > 0.0)
                printf("measured rate       : %.2f fps (written frames)\n",
                       (double)(written > 1 ? written - 1 : 1) / window);
            if (elapsed > 0.0) {
                printf("stream rate         : %.2f fps\n",
                       (double)asm_.stats.frames_ok / elapsed);
                printf("payload throughput  : %.2f MB/s\n", mb / elapsed);
            }
            printf("errors              : bad_header %llu, payload_err %llu, "
                   "no_pts %llu, size_mismatch %llu, restarts %llu\n",
                   (unsigned long long)asm_.stats.bad_header,
                   (unsigned long long)asm_.stats.payload_err,
                   (unsigned long long)asm_.stats.no_pts,
                   (unsigned long long)asm_.stats.size_mismatch,
                   (unsigned long long)asm_.stats.restarts);
        }

        ok = (written == cfg.frames);

        reader_destroy(rd);
        fa_free(&asm_);
    }

    ps3eye_close(&dev);
    return ok ? 0 : 1;
}
