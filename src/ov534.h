/* ov534.h — OV534 bridge + OV7720 sensor bring-up over WinUSB.
 *
 * Register tables and SCCB sequences are ported from the Linux kernel driver
 * drivers/media/usb/gspca/ov534.c (GPL-2.0-or-later). See docs/tasks.md,
 * "Licensing note".
 */
#ifndef OV534_H
#define OV534_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ps3eye_usb.h"

typedef enum {
    PS3EYE_MODE_VGA_640x480  = 0,
    PS3EYE_MODE_QVGA_320x240 = 1
} ps3eye_mode;

uint32_t ov534_width(ps3eye_mode m);
uint32_t ov534_height(ps3eye_mode m);
/* Bytes per frame for the YUY2 path: width * height * 2. */
uint32_t ov534_frame_bytes(ps3eye_mode m);
const char *ov534_mode_name(ps3eye_mode m);

typedef struct {
    ps3eye_dev *dev;
    uint16_t    sensor_id;
    uint8_t     fps;        /* frame rate actually selected */
    ps3eye_mode mode;
    bool        streaming;
    unsigned    usb_errors;
} ov534;

/* Reset bridge, detect the sensor, load the init tables, power the stream off.
 * Leaves the device idle but initialised. */
bool ov534_init(ov534 *c, ps3eye_dev *dev, char *err, size_t errlen);

/* Load the mode start tables, apply default image controls, start streaming. */
bool ov534_start(ov534 *c, ps3eye_mode mode, uint8_t fps, char *err, size_t errlen);

/* Stop streaming and turn the LED off. Safe to call more than once. */
bool ov534_stop(ov534 *c);

/* Raw register access, for diagnostics. */
bool ov534_bridge_read(ov534 *c, uint8_t reg, uint8_t *val);
bool ov534_bridge_write(ov534 *c, uint8_t reg, uint8_t val);
bool ov534_sccb_read(ov534 *c, uint8_t reg, uint8_t *val);
bool ov534_sccb_write(ov534 *c, uint8_t reg, uint8_t val);

#endif /* OV534_H */
