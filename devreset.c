// Copyright (C) 2020 Jesús A. Álvarez
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>
#include <unistd.h>

#define kExcAccMaxWait 5

// device filters set from the command line
static int gListOnly, gHaveLocation, gHaveRelease;
static UInt32 gLocationID;
static UInt16 gRelease;

int DeviceConnected (io_iterator_t iterator, int *failed);

static void Usage (const char *prog) {
  fprintf(stderr,
    "usage: %s [-n] [-l locationID] [-r release] productID vendorID\n"
    "  -n             list matching devices, do not reset them\n"
    "  -l locationID  only the device at this locationID\n"
    "  -r release     only devices with this release (bcdDevice)\n", prog);
  exit(1);
}

// Exit codes:
//   0  at least one device matched and every matched device was re-enumerated
//   1  usage, invalid arguments, or setup error
//   2  no device matched
//   3  at least one matched device failed
int main (int argc, char *argv[])
{
  int ch;
  unsigned long v;
  char *end;

  while ((ch = getopt(argc, argv, "nl:r:")) != -1) {
    switch (ch) {
      case 'n':
        gListOnly = 1;
        break;
      case 'l':
        v = strtoul(optarg, &end, 0);
        if (end == optarg || *end || v > 0xffffffff) {
          fprintf(stderr, "Invalid locationID\n");
          exit(1);
        }
        gLocationID = (UInt32)v;
        gHaveLocation = 1;
        break;
      case 'r':
        v = strtoul(optarg, &end, 0);
        if (end == optarg || *end || v > 0xffff) {
          fprintf(stderr, "Invalid release\n");
          exit(1);
        }
        gRelease = (UInt16)v;
        gHaveRelease = 1;
        break;
      default:
        Usage(argv[0]);
    }
  }

  if (argc - optind < 2) Usage(argv[0]);

  SInt32 productID, vendorID;
  productID = strtol(argv[optind], NULL, 0);
  vendorID = strtol(argv[optind+1], NULL, 0);

  if (productID <= 0 || productID > 0xffff || vendorID <= 0 || vendorID > 0xffff) {
    fprintf(stderr, "Invalid productID or vendorID\n");
    exit(1);
  }

  printf("Looking for productID=0x%04x vendorID=0x%04x\n", productID, vendorID);

  kern_return_t kerr;

  CFMutableDictionaryRef dict = IOServiceMatching(kIOUSBDeviceClassName);
  if (!dict) {
    fprintf(stderr, "could not create matching dictionary for device vendor=0x%04X,product=0x%04X\n", vendorID, productID);
    exit(1);
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
    exit(1);
  }
  int failed = 0;
  int found = DeviceConnected(iter, &failed);
  IOObjectRelease(iter);

  if (failed) return 3;
  if (!found) {
    fprintf(stderr, "No matching device found\n");
    return 2;
  }
  return 0;
}

IOReturn ConfigureDevice (IOUSBDeviceInterface245 **dev) {
  UInt8 nConf;
  IOUSBConfigurationDescriptorPtr confDescPtr;

  (*dev)->GetNumberOfConfigurations(dev, &nConf);
  if (nConf == 0) return kIOReturnError;

  if ((*dev)->GetConfigurationDescriptorPtr(dev, 0, &confDescPtr)) return kIOReturnError;
  if ((*dev)->SetConfiguration(dev, confDescPtr->bConfigurationValue)) return kIOReturnError;

  return kIOReturnSuccess;
}

int DeviceConnected (io_iterator_t iterator, int *failed) {
  kern_return_t kerr;
  io_service_t device;
  IOCFPlugInInterface **iodev;
  IOUSBDeviceInterface245 **dev = NULL;
  UInt16 vendor, product, version;
  HRESULT result;
  int u;
  int found = 0;

  while ((device = IOIteratorNext(iterator))) {
    // get device plugin
    kerr = IOCreatePlugInInterfaceForService(device, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &iodev, (SInt32*)&u);
    IOObjectRelease(device);
    if (kerr != kIOReturnSuccess) {
      fprintf(stderr, "could not create plug-in interface: %08x\n", kerr);
      (*failed)++;
      continue;
    }

    // get device interface
    result = (*iodev)->QueryInterface(iodev, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID245), (LPVOID)&dev);
    IODestroyPlugInInterface(iodev);
    if (result || !dev) {
      fprintf(stderr, "could not get device interface: %08x\n", (int)result);
      (*failed)++;
      continue;
    }

    // get device data
    (*dev)->GetDeviceVendor(dev, &vendor);
    (*dev)->GetDeviceProduct(dev, &product);
    (*dev)->GetDeviceReleaseNumber(dev, &version);
    UInt32 location = 0;
    (*dev)->GetLocationID(dev, &location);

    // skip devices that don't pass the filters, without opening them
    if ((gHaveLocation && location != gLocationID) || (gHaveRelease && version != gRelease)) {
      (*dev)->Release(dev);
      continue;
    }

    fprintf(stdout, "Found device vendor=0x%04X, product=0x%04X, version=0x%04X, locationID=0x%08X\n", vendor, product, version, (unsigned int)location);
    found++;

    if (gListOnly) {
      (*dev)->Release(dev);
      continue;
    }

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
      (*failed)++;
      (void) (*dev)->Release(dev);
      continue;
    }

    // configure device
    if (ConfigureDevice(dev) != kIOReturnSuccess) {
      fprintf(stderr, "could not configure device\n");
      (*failed)++;
      (*dev)->USBDeviceClose(dev);
      (*dev)->Release(dev);
      continue;
    }

    // reenumerate to device
    kerr = (*dev)->USBDeviceReEnumerate(dev, 0);
    if (kerr != kIOReturnSuccess) {
      fprintf(stdout, "USBDeviceReEnumerate: error %d\n", kerr);
      (*failed)++;
    }

    // close device
    (*dev)->USBDeviceClose(dev);
    (*dev)->Release(dev);
  }

  return found;
}
