// devreset: re-enumerate (simulate unplug/replug of) matching USB devices
//
// Based on reenumerate.c by Jesús A. Álvarez (GitHub: zydeco):
//   https://gist.github.com/zydeco/4ba02afa99d3c234c2dfa975fec0b102
// The original gist does not state a license. License confirmation was
// requested from the author here:
//   https://gist.github.com/zydeco/4ba02afa99d3c234c2dfa975fec0b102?permalink_comment_id=6429082#gistcomment-6429082
//
// Copyright (C) 2020 Jesús A. Álvarez
// Copyright (C) 2020 David Helkowski
// Copyright (C) 2026 Sebastian Cruz
//
// Changes made in this repository are licensed under the MIT License;
// see LICENSE.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "usb_ops.h"

static int usage (const char *prog) {
  fprintf(stderr,
    "usage: %s [-n] [-l locationID] [-r release] productID vendorID\n"
    "  -n             list matching devices, do not reset them\n"
    "  -l locationID  only the device at this locationID\n"
    "  -r release     only devices with this release (bcdDevice)\n", prog);
  return 1;
}

// Exit codes:
//   0  at least one device matched and every matched device was re-enumerated
//   1  usage, invalid arguments, or setup error
//   2  no device matched
//   3  at least one matched device failed
int devreset_run (int argc, char **argv, const usb_ops *ops)
{
  // device filters set from the command line
  int listOnly = 0, haveLocation = 0, haveRelease = 0;
  uint32_t locationID = 0;
  uint16_t release = 0;
  int ch;
  unsigned long v;
  char *end;

  // reset getopt so devreset_run can be called more than once
  optreset = 1;
  optind = 1;
  while ((ch = getopt(argc, argv, "nl:r:")) != -1) {
    switch (ch) {
      case 'n':
        listOnly = 1;
        break;
      case 'l':
        v = strtoul(optarg, &end, 0);
        if (end == optarg || *end || v > 0xffffffff) {
          fprintf(stderr, "Invalid locationID\n");
          return 1;
        }
        locationID = (uint32_t)v;
        haveLocation = 1;
        break;
      case 'r':
        v = strtoul(optarg, &end, 0);
        if (end == optarg || *end || v > 0xffff) {
          fprintf(stderr, "Invalid release\n");
          return 1;
        }
        release = (uint16_t)v;
        haveRelease = 1;
        break;
      default:
        return usage(argv[0]);
    }
  }

  if (argc - optind < 2) return usage(argv[0]);

  // 32-bit like the original SInt32, so out-of-range input wraps the same way
  int32_t productID, vendorID;
  productID = (int32_t)strtol(argv[optind], NULL, 0);
  vendorID = (int32_t)strtol(argv[optind+1], NULL, 0);

  if (productID <= 0 || productID > 0xffff || vendorID <= 0 || vendorID > 0xffff) {
    fprintf(stderr, "Invalid productID or vendorID\n");
    return 1;
  }

  printf("Looking for productID=0x%04x vendorID=0x%04x\n", (unsigned int)productID, (unsigned int)vendorID);

  usb_dev **devs;
  size_t count;
  if (ops->find((uint16_t)vendorID, (uint16_t)productID, &devs, &count) != 0)
    return 1;

  int failed = 0;
  int found = 0;
  for (size_t i = 0; i < count; i++) {
    usb_dev_info info;
    if (ops->describe(devs[i], &info) != 0) {
      failed++;
      continue;
    }

    // skip devices that don't pass the filters, without opening them
    if ((haveLocation && info.location != locationID) || (haveRelease && info.release != release))
      continue;

    fprintf(stdout, "Found device vendor=0x%04X, product=0x%04X, version=0x%04X, locationID=0x%08X\n", (unsigned int)info.vendor, (unsigned int)info.product, (unsigned int)info.release, (unsigned int)info.location);
    found++;

    if (listOnly) continue;

    if (ops->reset(devs[i]) != 0) failed++;
  }
  ops->free_list(devs, count);

  if (failed) return 3;
  if (!found) {
    fprintf(stderr, "No matching device found\n");
    return 2;
  }
  return 0;
}

#ifndef UNIT_TEST
int main (int argc, char *argv[])
{
  return devreset_run(argc, argv, &usb_ops_iokit);
}
#endif
