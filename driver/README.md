# libfprint-goodix538d

libfprint with the `goodixtls53xd` driver, for the Goodix `27c6:538d` fingerprint
reader in this Dell Latitude 3410 (Omarchy / Arch). Upstream libfprint has no
driver for this sensor: its only Goodix driver, `goodixmoc`, does not claim the
ID, so stock `fprintd` reports "No devices available".

Source: [`lbssousa/libfprint`](https://github.com/lbssousa/libfprint), branch
`goodix-538d-sigfm-gtls`, tag `v1.94.100-goodix538d.1` — same upstream 1.94.100
base as Arch's `libfprint-git`, SIGFM matcher in C (no OpenCV).

## Build and install

```bash
sudo pacman -S --needed meson gobject-introspection glib2-devel devtools
cd "/run/media/pc/MY Drive/Projects/libfprint-goodix538d"
BUILDDIR=/tmp/mkbuild PKGDEST="$PWD" makepkg -f --noconfirm
sudo pacman -U --ask 4 "libfprint-git-2:$(. ./PKGBUILD; echo $pkgver)-1-x86_64.pkg.tar.zst"
```

## The package name is deliberate

This builds as **`libfprint-git`** with **`epoch=2`**, not under its own name, and that
is what keeps it installed:

- `omarchy setup security fingerprint` starts with
  `omarchy-pkg-missing libfprint-git fprintd usbutils`, which runs
  `pacman -Q -- libfprint-git` — an exact-name query that ignores `provides`. Under
  any other name, that check failed and the script reinstalled the stock build over
  this driver, killing the reader (it did exactly that twice on 2026-10-30). Now the
  check passes and the menu entry goes straight to enrolment.
- Epoch 2 sits above the omarchy repo's `1:1.94.100.r10.g6f9479c-3`, so
  `pacman -Syu` and `omarchy update` offer no upgrade and never replace it. No
  `IgnorePkg` entry is needed.

Verify both any time:

```bash
pacman -Q libfprint-git                              # expect 2:1.94.100.goodix538d.1-1
omarchy-pkg-missing libfprint-git fprintd usbutils   # expect exit 1 (all present)
pacman -Qu libfprint-git                             # expect no output
```

`BUILDDIR` must be a path **without spaces**: makepkg passes `$srcdir` to
`-ffile-prefix-map`, and "MY Drive" breaks the compiler invocation. `options=(!debug)`
is set for the same reason.

Build deps: `meson gobject-introspection glib2-devel devtools` (the last one for
`arch-meson`).

### Why the two build flags

- `-Dintrospection=false` — `g-ir-scanner` is broken system-wide on this machine
  (Python 3.15 + setuptools 84 dropped the `distutils` shim). Only `fprintd`
  consumes libfprint here and it uses the C API, so the missing typelib costs
  nothing.
- `prepare()` patches `tests/meson.build`: on the no-introspection path the fork
  iterates the `drivers_tests` dict with one loop variable, which meson rejects.

### The PSK patch

`0001-goodixtls53xd-write-white-box-psk.patch` makes the driver write the
community white-box PSK when the sensor's PSK does not match, instead of only
comparing and failing. **The app firmware rejects that write (reply `0x01`)**, so
the patch cannot re-pair a sensor on its own — the pairing was done with
`goodix-fp-dump` (below). The patch is harmless now that the PSK matches, and it
is the right hook if the sensor is ever re-paired.

## How this sensor was paired (2026-10-30)

The sensor shipped paired with the OEM key (PSK hash `0d803259…d2a3`), while the
driver needs the all-zero PSK (hash `66687aad…2925`). Re-pairing needs IAP mode,
so `goodix-fp-dump`'s `run_538d.py` erased the app firmware, wrote the PSK in the
bootloader, and reflashed `firmware/53xd/GF5298_GM168SEC_APP_13016.bin`
(24268 B, sha256 `0f28f8f5…6c0d`) — the same version the sensor already ran.

That tool needed one fix, kept here as `driver_53xd.py.patched-for-538d`: its
`init_device()` only sent `nop()`, and this unit answers nothing until
`enable_chip(True)` is sent (the C driver and the tool's own 51x0/51x7 drivers do
this). Without it, `firmware_version()` fails with "Invalid message protocol".

Do this with the charger connected and `fprintd` **masked** — the Omarchy lock
plugin polls `fprintd-list`, which would claim the sensor mid-flash.

## Upkeep

No `IgnorePkg` entry is needed — the epoch handles it (see above). When upstream
libfprint moves and you want the newer base, check the fork for a newer tag,
rebuild, and refresh the patch if it stops applying. Keep the epoch at 2 or higher.

## PAM wiring

`sudo`, `/etc/pam.d/polkit-1` and `/etc/pam.d/omarchy-lock-fingerprint` are set up
exactly as `omarchy setup security fingerprint` would do it, and that command is now
safe to run (see the package-name section). Remove with
`omarchy remove security fingerprint`, which also drops `fprintd` and leaves
`libfprint-goodix538d` installed.
