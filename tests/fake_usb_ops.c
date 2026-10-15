// fake_usb_ops: a scriptable usb_ops for unit tests (see fake_usb_ops.h).
#include <stdlib.h>
#include <string.h>

#include "fake_usb_ops.h"

struct usb_dev { int index; };

typedef struct {
  usb_dev_info info;
  int describe_fails, reset_fails;
  int describe_calls, reset_calls;
} fake_dev;

static fake_dev g_devs[FAKE_MAX_DEVS];
static int g_ndevs;
static int g_find_fails, g_find_calls;
static int g_outstanding;

void fake_reset (void) {
  memset(g_devs, 0, sizeof g_devs);
  g_ndevs = 0;
  g_find_fails = 0;
  g_find_calls = 0;
  g_outstanding = 0;
}

int fake_add_device (uint16_t vendor, uint16_t product, uint16_t release, uint32_t location) {
  if (g_ndevs >= FAKE_MAX_DEVS) abort();
  fake_dev *f = &g_devs[g_ndevs];
  f->info.vendor = vendor;
  f->info.product = product;
  f->info.release = release;
  f->info.location = location;
  return g_ndevs++;
}

void fake_fail_find (void) { g_find_fails = 1; }
void fake_fail_describe (int i) { g_devs[i].describe_fails = 1; }
void fake_fail_reset (int i) { g_devs[i].reset_fails = 1; }
int  fake_reset_calls (int i) { return g_devs[i].reset_calls; }
int  fake_describe_calls (int i) { return g_devs[i].describe_calls; }
int  fake_find_calls (void) { return g_find_calls; }
int  fake_outstanding (void) { return g_outstanding; }

// Like IOKit matching: only devices with this vendor/product are returned.
static int fake_op_find (uint16_t vendor, uint16_t product, usb_dev ***devs, size_t *count) {
  g_find_calls++;
  if (g_find_fails) {
    g_find_fails = 0;
    return 1;
  }
  usb_dev **list = calloc(FAKE_MAX_DEVS, sizeof *list);
  if (!list) abort();
  g_outstanding++;
  size_t n = 0;
  for (int i = 0; i < g_ndevs; i++) {
    if (g_devs[i].info.vendor != vendor || g_devs[i].info.product != product)
      continue;
    usb_dev *d = malloc(sizeof *d);
    if (!d) abort();
    d->index = i;
    list[n++] = d;
    g_outstanding++;
  }
  *devs = list;
  *count = n;
  return 0;
}

static int fake_op_describe (usb_dev *dev, usb_dev_info *info) {
  fake_dev *f = &g_devs[dev->index];
  f->describe_calls++;
  if (f->describe_fails) return 1;
  *info = f->info;
  return 0;
}

static int fake_op_reset (usb_dev *dev) {
  fake_dev *f = &g_devs[dev->index];
  f->reset_calls++;
  return f->reset_fails ? 1 : 0;
}

static void fake_op_free_list (usb_dev **devs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(devs[i]);
    g_outstanding--;
  }
  free(devs);
  g_outstanding--;
}

const usb_ops fake_usb_ops = {
  fake_op_find, fake_op_describe, fake_op_reset, fake_op_free_list
};
