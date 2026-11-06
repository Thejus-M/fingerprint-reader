# fingerprint-reader

Everything that makes the Goodix `27c6:538d` fingerprint sensor work on this
Dell Latitude 3410 running Omarchy, plus the report of how it was done.

Stock libfprint has no driver for this sensor, and the sensor shipped paired
with the OEM key. Both had to be solved: a community fork supplies the driver,
and the sensor was re-paired to the community white-box PSK.

## Layout

```
fingerprint-reader/
├── README.md     this file
├── REPORT.md     full report: every route analysed, every fix, the chronology, revert paths
├── driver/       PKGBUILD, the two patches, the built package, driver/README.md
├── backup/       copies of the system files the login scripts touch
└── scripts/
    ├── fp-status           read-only health check of the whole setup
    ├── fp-restore-driver   reinstall the driver package if something replaced it
    ├── fp-pam-test         prove a PAM service (default sddm) accepts the finger
    ├── fp-login-enable     fingerprint at the SDDM greeter + autologin off
    ├── fp-login-disable    revert fp-login-enable
    └── fp-rebuild          rebuild the package from driver/PKGBUILD
```

`Projects/libfprint-goodix538d` is now a symlink to `driver/`, so older
references still resolve. Linked into `~/Work/fingerprint-reader`.

## Daily use

Fingerprint works for `sudo`, polkit dialogs and the Omarchy lock screen
(Super+Ctrl+L). Password still works everywhere.

```bash
~/Work/fingerprint-reader/scripts/fp-status    # is everything still in place?
fprintd-verify                                 # does the finger read?
fprintd-enroll "$USER"                         # add or replace a finger
```

## The one thing to remember

The driver is installed **as `libfprint-git` with `epoch=2`**. That is not
cosmetic: it is what stops `pacman -Syu`, `omarchy update` and the Omarchy
Fingerprint menu entry from replacing it with the stock build, which killed the
reader twice before. Keep the epoch at 2 or higher on any rebuild. Details in
`REPORT.md` §6.1.

## Boot login

Boot is: LUKS passphrase → SDDM greeter → desktop. The greeter takes the finger
once `scripts/fp-login-enable` has run (it also turns SDDM autologin off, since
an autologin greeter asks for nothing). Password still works there. Revert with
`scripts/fp-login-disable`, which is safe to run from Ctrl+Alt+F2.

## Not fixable

The LUKS passphrase at boot cannot be a fingerprint — `fprintd` is userspace and
the initramfs has no path to this sensor. See `REPORT.md` §7 for what the boot
chain actually asks and the options around it.
