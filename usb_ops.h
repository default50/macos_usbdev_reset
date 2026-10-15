// usb_ops: the hardware seam between devreset's logic and IOKit.
//
// devreset.c talks to USB devices only through a usb_ops table. The real
// table (usb_ops_iokit, in usb_ops_iokit.c) uses IOKit; the unit tests
// pass a scriptable fake instead (tests/fake_usb_ops.c).
#ifndef USB_OPS_H
#define USB_OPS_H

#include <stddef.h>
#include <stdint.h>

typedef struct usb_dev usb_dev;          /* opaque, owned by the ops impl */

typedef struct {
  uint16_t vendor;    /* GetDeviceVendor */
  uint16_t product;   /* GetDeviceProduct */
  uint16_t release;   /* GetDeviceReleaseNumber (bcdDevice) */
  uint32_t location;  /* GetLocationID */
} usb_dev_info;

typedef struct usb_ops {
  /* Snapshot of devices matching vendor/product. 0 = ok (count may be 0);
     nonzero = setup error, message already printed. On success the caller
     must call free_list(*devs, *count). */
  int  (*find)(uint16_t vendor, uint16_t product, usb_dev ***devs, size_t *count);
  /* Read device fields. 0 = ok; nonzero = device failed, message printed. */
  int  (*describe)(usb_dev *dev, usb_dev_info *info);
  /* Open (with exclusive-access retry), configure, ReEnumerate, close.
     Only called after a successful describe. 0 = ok; nonzero = failed,
     message printed. */
  int  (*reset)(usb_dev *dev);
  /* Release every handle and the array. */
  void (*free_list)(usb_dev **devs, size_t count);
} usb_ops;

extern const usb_ops usb_ops_iokit;

/* Parse arguments, find and reset (or list) matching devices through ops,
   and return the exit code (0, 1, 2, or 3; see devreset.c). */
int devreset_run(int argc, char **argv, const usb_ops *ops);

#endif
