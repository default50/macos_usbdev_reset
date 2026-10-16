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

```sh
make
```

This builds a single universal `devreset` (x86_64 for macOS 10.12+,
arm64 for macOS 11+). `make clean` removes it and the test binary.

## Testing

```sh
make test
```

This builds `tests/test_devreset` for your native architecture and runs
it. No frameworks are linked: a fake (`tests/fake_usb_ops.c`) stands in
for IOKit, so no USB device is touched. The tests cover usage errors,
exit codes, once-per-device reset of devices that share a VID/PID, and
`-n`/`-l`/`-r` selection. A real reset still needs hardware.

The tests use [greatest](https://github.com/silentbicycle/greatest)
v1.5.0 (ISC), vendored unmodified in `tests/greatest.h`.

## Local checks (pre-commit hook)

`.githooks/pre-commit` runs `make test`, lints all tracked Markdown with
[markdownlint-cli2](https://github.com/DavidAnson/markdownlint-cli2)
(config in `.markdownlint.jsonc`) and lints the hook and `yubireset`
with [shellcheck](https://www.shellcheck.net/). A failure blocks the commit.
Enable it once per clone:

```sh
git config core.hooksPath .githooks
```

Requires `markdownlint-cli2` and `shellcheck` on your `PATH`
(`brew install markdownlint-cli2 shellcheck`). The hook checks the
working tree, not just the staged changes. `git commit --no-verify`
skips it once.

## Usage

```text
devreset [-n] [-l locationID] [-r release] productID vendorID
  -n             list matching devices, do not reset them
  -l locationID  only the device at this locationID
  -r release     only devices with this release (bcdDevice)
```

IDs can be hex (`0x...`) or decimal. Options go before the IDs. Every
matching device is reset once.

```sh
./devreset -n 0x0407 0x1050               # list matches, reset nothing
./devreset 0x0407 0x1050                  # reset every match
./devreset -l 0x01100000 0x0407 0x1050    # reset only the one at that location
```

`-n` prints one line per match:

```text
Looking for productID=0x0407 vendorID=0x1050
Found device vendor=0x1050, product=0x0407, version=0x0543, locationID=0x01100000
Found device vendor=0x1050, product=0x0407, version=0x0574, locationID=0x00100000
```

## yubireset

`yubireset` is a small bash wrapper around devreset for YubiKeys. The
vendor ID is always Yubico's (`0x1050`) and the product ID defaults to
`0x0407` (YubiKey 5 OTP+FIDO+CCID).

```text
yubireset [-p productID] [-n] [-l locationID] [-r release]
  -p productID   YubiKey product ID (default 0x0407)
  -n             list matching YubiKeys, do not reset them
  -l locationID  only the YubiKey at this locationID
  -r release     only YubiKeys with this release (bcdDevice)
```

```sh
./yubireset -n                  # list connected YubiKeys
./yubireset                     # reset every YubiKey 5 OTP+FIDO+CCID
./yubireset -l 0x01100000       # reset only the one at that location
./yubireset -p 0x0402           # another product ID
```

It uses the `devreset` next to it (symlinks are followed, so you can
link it into `~/bin`), or else one on your `PATH`. Exit codes are
devreset's, with a hint printed for 2 and 3.

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
| --- | --- |
| 0 | At least one device matched and every matched device was re-enumerated (with `-n`: at least one device matched) |
| 1 | Usage, invalid arguments, or setup error |
| 2 | No device matched |
| 3 | At least one matched device failed |

## Caveats

- If another process holds the whole device open exclusively (some vendor
  apps do), devreset waits about 5 seconds (`waiting for access`) and
  then fails with exit 3 (`could not open device: e00002c5`). Quit that
  process first. Smartcard services (`scdaemon`, macOS CryptoTokenKit)
  only claim the CCID interface and do not block a reset; tested on
  macOS 26 with `scdaemon` running.
- Root is usually not required. All testing ran as a normal user.
- The device disappears briefly and comes back with a new IOKit registry
  entry.

## License

MIT, see [LICENSE](LICENSE).
