/* probe_main.c — ps3eye-probe
 *
 * Dumps the real USB configuration, the WinUSB pipe map and the sensor
 * identity. Read-only unless --identify is passed (the default), because
 * identifying the sensor requires resetting the bridge.
 */
#include <stdio.h>
#include <string.h>

#include "ov534.h"
#include "ps3eye_usb.h"

static void usage(void)
{
    printf("usage: ps3eye-probe [--descriptors-only]\n"
           "\n"
           "  --descriptors-only  do not touch the device; only read the USB\n"
           "                      configuration and the WinUSB pipe map\n");
}

int main(int argc, char **argv)
{
    bool        identify = true;
    char        err[512];
    ps3eye_dev  dev;
    int         i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--descriptors-only") == 0) {
            identify = false;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (!ps3eye_open(&dev, err, sizeof(err))) {
        fprintf(stderr, "cannot open PS3 Eye: %s\n", err);
        return 1;
    }

    printf("PS3 Eye (VID_%04x&PID_%04x) opened through WinUSB\n\n",
           PS3EYE_VID, PS3EYE_PID);

    ps3eye_dump_descriptors(&dev, stdout);

    if (identify) {
        ov534      cam;
        static const struct {
            uint8_t     reg;
            const char *name;
        } regs[] = {
            { 0x12, "COM7  (format)" },
            { 0x13, "COM8  (agc/aec/awb)" },
            { 0x0c, "COM3  (flip)" },
            { 0x11, "CLKRC (clock)" },
            { 0x0d, "COM4  (pll)" },
            { 0x00, "GAIN" },
            { 0x10, "AEC" },
            { 0x63, "AWB b" },
            { 0x9b, "BLC   (brightness)" },
            { 0x9c, "CNST  (contrast)" },
        };
        size_t k;

        printf("\nidentifying sensor (this resets the bridge and writes registers)\n");

        if (!ov534_init(&cam, &dev, err, sizeof(err)))
            printf("ov534_init reported: %s\n", err);

        if (cam.sensor_id != 0) {
            printf("sensor ID            : 0x%04x\n", cam.sensor_id);
            if ((cam.sensor_id & 0xfff0) == 0x7720)
                printf("sensor               : OV7720/OV7725 family (YUY2 path)\n");
            else if ((cam.sensor_id & 0xfff0) == 0x7670)
                printf("sensor               : OV7670 family (JPEG, unsupported)\n");
            else
                printf("sensor               : unrecognised\n");

            printf("\nregister snapshot:\n");
            for (k = 0; k < sizeof(regs) / sizeof(regs[0]); k++) {
                uint8_t v = 0;
                if (ov534_sccb_read(&cam, regs[k].reg, &v))
                    printf("  0x%02x %-20s = 0x%02x\n", regs[k].reg, regs[k].name, v);
                else
                    printf("  0x%02x %-20s = read failed\n", regs[k].reg, regs[k].name);
            }
            printf("\nUSB/SCCB errors      : %u\n", cam.usb_errors);
            ov534_stop(&cam);
        }
    }

    ps3eye_close(&dev);
    return 0;
}
