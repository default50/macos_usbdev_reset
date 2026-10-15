// fake_usb_ops: a scriptable usb_ops for unit tests. No hardware, no
// output. Configure devices, inject failures, then read the call counts.
#ifndef FAKE_USB_OPS_H
#define FAKE_USB_OPS_H

#include <stdint.h>

#include "usb_ops.h"

#define FAKE_MAX_DEVS 16

extern const usb_ops fake_usb_ops;

void fake_reset(void);                       /* clear all state */
int  fake_add_device(uint16_t vendor, uint16_t product,
                     uint16_t release, uint32_t location); /* returns index */
void fake_fail_find(void);                   /* next find returns 1 */
void fake_fail_describe(int i);              /* describe of device i returns 1 */
void fake_fail_reset(int i);                 /* reset of device i returns 1 */
int  fake_reset_calls(int i);                /* per-device reset call count */
int  fake_describe_calls(int i);
int  fake_find_calls(void);
int  fake_outstanding(void);                 /* handles+lists allocated minus freed */

#endif
