// usb_ops_iokit: the real usb_ops implementation on top of IOKit.
//
// Based on reenumerate.c by Jesús A. Álvarez (GitHub: zydeco). See
// devreset.c for origin, copyright, and license notes.
#include <stdio.h>
#include <stdlib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>
#include <unistd.h>

#include "usb_ops.h"

#define kExcAccMaxWait 5

struct usb_dev {
  io_service_t service;
  IOUSBDeviceInterface245 **dev;
};

static void iokit_free_list (usb_dev **devs, size_t count);

static int iokit_find (uint16_t vendor, uint16_t product, usb_dev ***devs, size_t *count) {
  kern_return_t kerr;
  SInt32 vendorID = vendor, productID = product;

  CFMutableDictionaryRef dict = IOServiceMatching(kIOUSBDeviceClassName);
  if (!dict) {
    fprintf(stderr, "could not create matching dictionary for device vendor=0x%04X,product=0x%04X\n", (unsigned int)vendorID, (unsigned int)productID);
    return 1;
  }
  // the dictionary retains its values, so release our references
  CFNumberRef vendorNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vendorID);
  CFNumberRef productNum = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &productID);
  CFDictionarySetValue(dict, CFSTR(kUSBVendorID), vendorNum);
  CFDictionarySetValue(dict, CFSTR(kUSBProductID), productNum);
  CFRelease(vendorNum);
  CFRelease(productNum);
  // IOServiceGetMatchingServices consumes dict and returns a snapshot of the
  // devices matching now, so devices that come back after being re-enumerated
  // are not picked up (and reset) again.
  // MACH_PORT_NULL selects the default main port. kIOMainPortDefault is
  // not used because it needs macOS 12 and the x86_64 build targets 10.12.
  io_iterator_t iter;
  kerr = IOServiceGetMatchingServices(MACH_PORT_NULL, dict, &iter);
  if (kerr != kIOReturnSuccess) {
    fprintf(stderr, "IOServiceGetMatchingServices\n");
    return 1;
  }

  // drain the snapshot into a list; per-device work happens in describe
  usb_dev **list = NULL;
  size_t n = 0, cap = 0;
  io_service_t service;
  while ((service = IOIteratorNext(iter))) {
    usb_dev *d = NULL;
    if (n == cap) {
      size_t ncap = cap ? cap * 2 : 4;
      usb_dev **nlist = realloc(list, ncap * sizeof *nlist);
      if (nlist) {
        list = nlist;
        cap = ncap;
      }
    }
    if (n < cap) d = calloc(1, sizeof *d);
    if (!d) {
      IOObjectRelease(service);
      iokit_free_list(list, n);
      IOObjectRelease(iter);
      fprintf(stderr, "out of memory\n");
      return 1;
    }
    d->service = service;
    list[n++] = d;
  }
  IOObjectRelease(iter);

  *devs = list;
  *count = n;
  return 0;
}

static int iokit_describe (usb_dev *d, usb_dev_info *info) {
  kern_return_t kerr;
  IOCFPlugInInterface **iodev;
  IOUSBDeviceInterface245 **dev = NULL;
  UInt16 vendor, product, version;
  HRESULT result;
  SInt32 score;

  // get device plugin
  kerr = IOCreatePlugInInterfaceForService(d->service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &iodev, &score);
  IOObjectRelease(d->service);
  d->service = 0;
  if (kerr != kIOReturnSuccess) {
    fprintf(stderr, "could not create plug-in interface: %08x\n", kerr);
    return 1;
  }

  // get device interface
  result = (*iodev)->QueryInterface(iodev, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID245), (LPVOID)&dev);
  IODestroyPlugInInterface(iodev);
  if (result || !dev) {
    fprintf(stderr, "could not get device interface: %08x\n", (int)result);
    return 1;
  }
  d->dev = dev;

  // get device data
  (*dev)->GetDeviceVendor(dev, &vendor);
  (*dev)->GetDeviceProduct(dev, &product);
  (*dev)->GetDeviceReleaseNumber(dev, &version);
  UInt32 location = 0;
  (*dev)->GetLocationID(dev, &location);

  info->vendor = vendor;
  info->product = product;
  info->release = version;
  info->location = location;
  return 0;
}

static IOReturn ConfigureDevice (IOUSBDeviceInterface245 **dev) {
  UInt8 nConf;
  IOUSBConfigurationDescriptorPtr confDescPtr;

  (*dev)->GetNumberOfConfigurations(dev, &nConf);
  if (nConf == 0) return kIOReturnError;

  if ((*dev)->GetConfigurationDescriptorPtr(dev, 0, &confDescPtr)) return kIOReturnError;
  if ((*dev)->SetConfiguration(dev, confDescPtr->bConfigurationValue)) return kIOReturnError;

  return kIOReturnSuccess;
}

static int iokit_reset (usb_dev *d) {
  kern_return_t kerr;
  IOUSBDeviceInterface245 **dev = d->dev;
  int u;
  int failed = 0;

  // open device
  u = 0;
  do {
    kerr = (*dev)->USBDeviceOpen(dev);
    if (kerr == kIOReturnExclusiveAccess) {
      fprintf(stdout, "waiting for access (%d)\n", kExcAccMaxWait-u);
      u++;
      sleep(1);
    }
  } while ((kerr == kIOReturnExclusiveAccess) && (u < kExcAccMaxWait));

  if (kerr != kIOReturnSuccess) {
    fprintf(stderr, "could not open device: %08x\n", kerr);
    (void) (*dev)->Release(dev);
    d->dev = NULL;
    return 1;
  }

  // configure device
  if (ConfigureDevice(dev) != kIOReturnSuccess) {
    fprintf(stderr, "could not configure device\n");
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
    d->dev = NULL;
    return 1;
  }

  // reenumerate to device
  kerr = (*dev)->USBDeviceReEnumerate(dev, 0);
  if (kerr != kIOReturnSuccess) {
    fprintf(stdout, "USBDeviceReEnumerate: error %d\n", kerr);
    failed = 1;
  }

  // close device
  (*dev)->USBDeviceClose(dev);
  (*dev)->Release(dev);
  d->dev = NULL;
  return failed;
}

static void iokit_free_list (usb_dev **devs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    usb_dev *d = devs[i];
    if (d->dev) (*d->dev)->Release(d->dev);
    if (d->service) IOObjectRelease(d->service);
    free(d);
  }
  free(devs);
}

const usb_ops usb_ops_iokit = {
  iokit_find, iokit_describe, iokit_reset, iokit_free_list
};
