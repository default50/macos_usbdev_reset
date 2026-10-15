# macos_usbdev_reset

`devreset` does a software unplug and replug of a USB device on macOS. It
asks IOKit to re-enumerate the device (`USBDeviceReEnumerate`), which is
useful when a device gets stuck and you can't physically replug it.

## Credits

The code was originally written by Jesús A. Álvarez (zydeco) in
[this gist](https://gist.github.com/zydeco/4ba02afa99d3c234c2dfa975fec0b102)
and packaged in
[nanoscopic/macos_usbdev_reset](https://github.com/nanoscopic/macos_usbdev_reset).
This fork adds the single-reset fix, device selection, exit codes, and a
universal build.

## Build

Requires the Xcode Command Line Tools.

```
make
```

This builds `devreset_x86` (x86_64, macOS 10.12+) and `devreset_arm`
(arm64, macOS 11+) and combines them with `lipo` into a universal
`devreset`. `make clean` removes all three.

## Usage

```
devreset [-n] [-l locationID] [-r release] productID vendorID
  -n             list matching devices, do not reset them
  -l locationID  only the device at this locationID
  -r release     only devices with this release (bcdDevice)
```

IDs can be hex (`0x...`) or decimal. Options go before the IDs. Every
matching device is reset once.

```
./devreset -n 0x0407 0x1050               # list matches, reset nothing
./devreset 0x0407 0x1050                  # reset every match
./devreset -l 0x01100000 0x0407 0x1050    # reset only the one at that location
```

`-n` prints one line per match:

```
Looking for productID=0x0407 vendorID=0x1050
Found device vendor=0x1050, product=0x0407, version=0x0543, locationID=0x01100000
Found device vendor=0x1050, product=0x0407, version=0x0574, locationID=0x00100000
```

## Finding IDs and locationIDs

- `devreset -n productID vendorID` shows version and locationID in hex.
- `system_profiler SPUSBHostDataType` (macOS 15+; `SPUSBDataType` on older
  releases) shows `USB Vendor ID`, `USB Product ID`, `USB Product Version`,
  and `Location ID` in hex. System Information > USB shows the same.
- `ioreg -p IOUSB -w0 -l` shows `idVendor`, `idProduct`, `bcdDevice`, and
  `locationID` in decimal. Convert with `printf '0x%04x\n' 4176`.

A locationID depends on the port and hub, so it can change when you
replug the device. The release number (bcdDevice) can tell apart
identical devices that run different firmware.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | At least one device matched and every matched device was re-enumerated |
| 1 | Usage, invalid arguments, or setup error |
| 2 | No device matched |
| 3 | At least one matched device failed |

## Caveats

- If another process holds the device open exclusively (smartcard
  daemons such as `pcscd` or `scdaemon`, vendor apps), devreset waits
  about 5 seconds (`waiting for access`) and then fails with exit 3. Quit
  or kill that process first.
- Root is usually not required. All testing ran as a normal user.
- The device disappears briefly and comes back with a new IOKit registry
  entry.

## License

MIT, see [LICENSE](LICENSE).
