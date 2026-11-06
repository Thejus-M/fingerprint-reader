# Fingerprint reader on this machine — full report

**Machine:** Dell Latitude 3410, Omarchy 4.0.6-1 (Arch), Hyprland + SDDM, root on LUKS/btrfs.
**Sensor:** Goodix `27c6:538d`, press-type, TLS-PSK protocol, no vendor Linux driver.
**Work done:** 2026-09-11 (investigation only) and 2026-10-30 (the fix).
**Report written:** 2026-11-02.

---

## 1. Current state

| Item | Value |
|---|---|
| Driver package | `libfprint-git 2:1.94.100.goodix538d.1-1` (the community fork, installed under the stock package name) |
| Omarchy repo version it displaces | `libfprint-git 1:1.94.100.r10.g6f9479c-3` |
| `fprintd` | `1.94.5-2`, service active |
| Reader as seen by fprintd | `Goodix TLS Fingerprint Sensor 53XD (press)` |
| Enrolled | user `pc`, `#0: right-index-finger` |
| Sensor PSK | community white-box key (hash `66687aad…2925`); the OEM key (`0d803259…d2a3`) was overwritten and cannot be restored |
| Fingerprint works for | `sudo`, polkit dialogs, Omarchy lock screen (Super+Ctrl+L) |
| Fingerprint does **not** work for | LUKS passphrase at boot (impossible, see §7) |
| SDDM greeter | PAM wired for fingerprint; **autologin still bypasses it** until `scripts/fp-login-enable` is re-run with the 2026-11-05 fix (§9.1) |
| Password fallback | intact everywhere |

Verify all of this in one command: `~/Work/fingerprint-reader/scripts/fp-status`.

---

## 2. Why the stock system cannot read this sensor

* `27c6:538d` is a Goodix match-on-chip sensor that speaks a TLS-PSK channel. Goodix never published a Linux driver or a spec; the protocol was reverse engineered from the Windows driver by the community.
* Upstream libfprint has exactly one Goodix driver, `goodixmoc`, and `538d` is not in its ID table. So `fprintd-list pc` answers `No devices available` even though the device enumerates on USB and `omarchy-hw-fingerprint` returns success (that check only proves a fingerprint-class USB device is present).
* This is an upstream gap, not an Omarchy misconfiguration. Open since 2020: <https://bugs.launchpad.net/bugs/1879247>.
* Nothing in Omarchy blocks the sensor — there is no licence check, no DRM, no account gate. The menu entry appears because the hardware check passes, and then enrolment fails because libfprint has no code that can talk to the chip.

---

## 3. Every route that was analysed

| Route | Verdict |
|---|---|
| Stock `libfprint` / `libfprint-git` (`goodixmoc`) | Cannot work. `538d` absent from the ID table. |
| AUR `libfprint-goodix-521d` | Rejected. Claims the 538d family but is based on libfprint 1.94.1, untouched since Dec 2024 — a downgrade against `fprintd 1.94.5`. |
| Ubuntu TOD blob (`libfprint-2-tod1-goodix-*`) | Dead end. Launchpad `libfprint-tod1-group` ships `550a` only, never `538d`. Dell ships no Linux fingerprint driver for the Latitude 3410. |
| Extracting the key from a local Windows install | Not available. No Windows partition on this machine (the internal ESP holds BOOT/Dell/limine/Linux only; `sda1` is an old Fedora ESP). |
| **Fork `lbssousa/libfprint`, branch `goodix-538d-sigfm-gtls`, tag `v1.94.100-goodix538d.1`** | **Chosen.** Driver `goodixtls53xd`, ID table `{0x27c6, 0x538d}`, same 1.94.100 base as the Omarchy package, active as of Oct 2026, SIGFM matcher rewritten in C so OpenCV is not needed. |
| Non-destructive PSK write through the fork's own C code | Tried, refused by the device. See §5, step 6. |
| `goodix-fp-dump` erase + PSK write + reflash (IAP mode) | Used, and it is what finally paired the sensor. Irreversible. See §5, step 8. |
| Fingerprint for the LUKS passphrase | Impossible with fprintd. See §7. |

Two hard gates live in the fork's driver, and both had to be satisfied: the firmware string must equal `GF5298_GM168SEC_APP_13016`, and the sensor's PSK must match the driver's hard-coded white-box key. The firmware matched out of the box; the PSK did not.

---

## 4. Safety net built before anything was changed

1. Snapper snapshot **#8** (`omarchy snapshot create`); limine + `limine-snapper-sync.service` are enabled, so it is selectable from the boot menu.
2. `/run/media/pc/MY Drive/Backup/fingerprint-20261030-120432/` holding `pam.d-and-pacman.conf.tgz` plus the stock `libfprint-git` package file.
3. The stock package also stays in `/var/cache/pacman/pkg/` — its own btrfs subvolume `@pkg`, so a rollback of `/` does not take it with it.
4. `.bak` copy of each PAM file before it was touched (e.g. `/etc/pam.d/sudo.bak.1793344636`).
5. Ordering chosen so lockout was never on the table: driver proven and finger verified **before** any PAM file was edited; then lock screen (the plugin refuses to lock on a broken stack), then polkit (worst case: GUI dialogs stop, `sudo` still works), then `sudo` last — with a live `sudo -i` root shell held open and verification from a separate terminal. `passwd -S root` was checked in that shell first, because Arch locks the root account by default.

---

## 5. What was actually done, in order (2026-10-30, times IST)

1. **11:41 — Mapped the stack.** Confirmed reader ID, confirmed stock libfprint has no driver, confirmed login is SDDM autologin over a LUKS root, found Omarchy's own wiring (`omarchy-setup-security-fingerprint`, `omarchy-apply-lock`, the lock plugin's fingerprint support in `Service.qml`).
2. **12:04 — Built the fork as an Arch package.** `PKGBUILD` written against tag `v1.94.100-goodix538d.1`. Three build deviations, all forced:
   * `BUILDDIR=/tmp/mkbuild` and `options=(!debug)` — the space in `MY Drive` breaks makepkg's `-ffile-prefix-map`.
   * `-Dintrospection=false` — `g-ir-scanner` is broken system-wide here (Python 3.15 + setuptools 84 dropped the `distutils` shim). Only `fprintd` consumes libfprint on this machine and it uses the C API, so the missing typelib costs nothing.
   * A one-line `prepare()` patch to `tests/meson.build` — on the no-introspection path the fork iterates the `drivers_tests` dict with a single loop variable, which meson rejects.
3. **12:10 — Installed, driver bound.** `fprintd-list` now named the device: `Goodix TLS Fingerprint Sensor 53XD`. Firmware check passed.
4. **12:20 — Enrolment failed on the PSK.** `fprintd-enroll pc` →
   `failed to claim device: ... Invalid device PSK: 0x0d803259866ce60e1e3d1375b7fd7ebb7f594cd7f6c219e6d62da0d0a648d2a3`.
   The sensor was still paired with the OEM key; the driver expects the community white-box key (hash `66687aad…2925`). The device only ever returns a *hash* of its PSK, never the plaintext — which is why the original pairing could not be saved.
5. **12:22 — Read what the reference tool does.** For this exact state (firmware correct, PSK invalid), `goodix-fp-dump`'s `driver_53xd.main()` erases the application firmware, which drops the chip into its IAP bootloader, then writes the PSK and reflashes `firmware/53xd/GF5298_GM168SEC_APP_13016.bin` (24268 B, sha256 `0f28f8f5…6c0d`) — the same firmware version the sensor already ran. Risks were written out in plain terms before asking: the brick window sits between erase and successful flash; the IAP bootloader itself is not written to and accepts re-flashing, so most failures are retryable; a true brick needs the bootloader to fail, which realistically means power loss. Charger was plugged in for this reason.
6. **12:35 — Tried the non-destructive route first.** The fork's C code already contains `goodix_send_preset_psk_write` and its receive handler, unused by the 53xd driver. Since that C implementation demonstrably talks to this sensor, the driver was patched to build the exact frame this device wants (10-byte pre-flags + flags + length + 96-byte white-box PSK, wrapped in total/length/offset header — 126 bytes) and write the PSK during open, retry once, then re-read to verify. The write reached the device and the device answered `0x01` = **rejected**: the application firmware will not accept a PSK write. That is precisely why the reference tool writes the PSK only in IAP mode, after erasing. The patch is kept (`driver/0001-goodixtls53xd-write-white-box-psk.patch`) — harmless now that the PSK matches, and the right hook if the sensor is ever re-paired.
7. **12:30–12:40 — Fixed `goodix-fp-dump` for this unit.** The tool failed before any destructive step, at `firmware_version()`, with `Invalid message protocol` — the ACK's command id did not match. A USB hard reset and masking `fprintd` did not help, so it was not stale device state. Cause: the tool's `init_device()` for 53xd sends only `nop()`, while the working C open sequence is `nop → enable_chip(TRUE) → nop → firmware_version`; the tool's own 51x0/51x7 drivers do call `enable_chip`. A read-only probe script confirmed it:
   ```
   FIRMWARE: GF5298_GM168SEC_APP_13016
   PSK REPLY: True 0xbb020001 0d803259…d2a3
   ```
   The fix — `enable_chip` added to `init_device`, with failure tolerated so the IAP-mode recovery path still works — is kept as `driver/driver_53xd.py.patched-for-538d`. Repo was pinned at commit `cc43bb3` (2023-05-30), firmware submodule `7b9a828`, venv on CPython 3.12.14 via uv; `spidev==3.8` had to be installed because `protocol.py` imports it unconditionally even for USB.
8. **12:44 — Pairing flashed (user ran it; root + freshly downloaded third-party code is outside what the agent may launch).** `fprintd` masked for the duration, because the Omarchy lock plugin polls `fprintd-list` and would claim the sensor mid-flash. The log shows the full chain: erase → IAP (`MILAN_GM168SEC_IAP_10007`) → PSK write → 24 KB firmware written in 256-byte chunks → HMAC check passed → reset → re-enumerated → `Firmware: GF5298_GM168SEC_APP_13016`, **`Valid PSK: True`**, then a live TLS session capturing 80×64 images from the finger.
9. **12:46 — Enrolled.** `fprintd-enroll pc` → `#0: right-index-finger`, `fprintd-verify` passed.
10. **12:47 — PAM wired** in the safety order of §4, and verified one stack at a time. Content is byte-identical to what `omarchy setup security fingerprint` writes (§6).
11. **12:50 — First regression.** The Omarchy menu's Fingerprint entry was run, whose first step is
    `pacman -S --needed --noconfirm --ask 4 -- libfprint-git fprintd usbutils`.
    That removed the fork package and reinstalled stock `libfprint-git`, so the reader died again (`No devices available`).
12. **12:53–13:00 — Reverted to the Omarchy default stack on request**, including the `IgnorePkg` line that had been added to `/etc/pacman.conf`, the stale print in `/var/lib/fprint/pc/`, the fork's calibration file, and the build dependencies (`meson`, `gobject-introspection`, `glib2-devel`, `devtools`).
13. **13:10 — Second regression, same cause.** The menu entry ran again and removed the driver a second time. At that point no finger was enrolled and the driver was gone — which is the answer to "why could I not unlock with the finger": nothing had ever been stored.
14. **13:14 — The permanent fix (§6.1).** Rebuilt the fork **as `libfprint-git` with `epoch=2`**, so Omarchy's own check sees the package as present and skips the install, and pacman never downgrades to the repo's `1:` version.
15. **13:16 — Verified and documented.** Driver installed, reader detected, protections confirmed, `driver/README.md` written, build packages removed again.

---

## 6. Every fix, file by file

### 6.1 The package-name / epoch trick — the fix that makes this stick

`omarchy-setup-security-fingerprint` starts with `omarchy-pkg-missing libfprint-git fprintd usbutils`, which runs `pacman -Q -- libfprint-git`: an **exact-name** query that ignores `provides`. Under any other package name that check failed, so the script reinstalled stock libfprint over the working driver — it did exactly that twice. Building the fork *as* `libfprint-git` with `epoch=2` fixes both failure modes at once:

* `omarchy-pkg-missing` passes, so the menu entry skips the package step and goes straight to enrolment. The menu entry is safe to use now.
* Epoch `2:` outranks the repo's `1:1.94.100.r10.g6f9479c-3`, and pacman never downgrades, so `pacman -Syu` and `omarchy update` offer no upgrade and cannot replace the driver. No `IgnorePkg` entry is needed, and `/etc/pacman.conf` is stock.

`PKGBUILD` also keeps `provides=(libfprint=1.94.100 libfprint-goodix538d=$pkgver libfprint-2.so)` and `conflicts=(libfprint)`.

### 6.2 Build fixes

| Problem | Fix |
|---|---|
| Space in `MY Drive` breaks `-ffile-prefix-map` | build with `BUILDDIR=/tmp/mkbuild`, `options=(!debug)` |
| `g-ir-scanner` broken system-wide (Python 3.15 / setuptools 84, no `distutils`) | `-Dintrospection=false` |
| Fork bug: `drivers_tests` dict iterated with one loop variable | `sed` patch to `tests/meson.build` in `prepare()` |

### 6.3 Patches kept in `driver/`

* `0001-goodixtls53xd-write-white-box-psk.patch` — makes the driver write the white-box PSK when the device's PSK does not match, instead of only comparing and failing. The app firmware rejects that write (`0x01`), so it cannot re-pair a sensor by itself; it is harmless while the PSK matches and is the correct hook after a re-pair.
* `driver_53xd.py.patched-for-538d` — the `goodix-fp-dump` fix (`enable_chip(True)` in `init_device`). Without it the tool dies at `firmware_version()` with `Invalid message protocol` on this unit.

### 6.4 PAM wiring (identical to Omarchy's own)

`/etc/pam.d/sudo` and `/etc/pam.d/polkit-1` each begin with:

```
auth      [success=1 default=ignore] pam_exec.so quiet /usr/bin/omarchy-hw-laptop-closed
auth      sufficient pam_fprintd.so
```

The first line is Omarchy's clamshell gate: with the lid shut the reader is unreachable, so PAM skips `pam_fprintd` (`success=1`) and goes straight to the password prompt instead of blocking on the reader until it times out.

`/etc/pam.d/omarchy-lock-fingerprint` (used by the lock screen):

```
#%PAM-1.0
auth       required                    pam_fprintd.so
account    include                     system-local-login
```

Password remains the fallback in all three. `omarchy remove security fingerprint` undoes all of it (and also drops `fprintd`).

### 6.5 Hardware change (not reversible)

The sensor's pairing key is now the community white-box PSK. The OEM key was readable only as a hash, so it cannot be written back. Nothing else on this machine uses the sensor and there is no Windows install, so the practical cost is zero; a future Windows install would re-pair Windows Hello by itself.

---

## 7. "After a shutdown I am never asked for the fingerprint" — why

The boot chain on this machine is:

1. **limine → initramfs → LUKS passphrase** for `/dev/nvme0n1p2`. This is the only credential prompt at boot, and it is almost certainly the one in question.
2. **SDDM with autologin** — `/etc/sddm.conf.d/autologin.conf` sets `User=pc`, `Session=omarchy.desktop`. The journal confirms it authenticates through the `sddm-autologin` PAM stack with no prompt at all. So the greeter never asks for anything, fingerprint or password.
3. **Hyprland.** Fingerprint is active from here on: `sudo`, polkit, and the lock screen.

Two separate facts follow:

* **The LUKS passphrase cannot be replaced by a fingerprint.** `fprintd` is a userspace daemon talking to the sensor over libusb, and this sensor additionally needs a TLS-PSK session; none of that exists in the initramfs. `systemd-cryptenroll` can delegate unlocking to a TPM2 chip, a FIDO2 key, or a PKCS#11 token — never to a fingerprint reader. Any "fingerprint unlocks my disk" setup elsewhere is really TPM2-or-FIDO2 unlock with a fingerprint gate afterwards.
* **The user login after boot asks for nothing today**, because autologin is on. To be asked for a fingerprint there, autologin has to go and `/etc/pam.d/sddm` has to get the same two lines as `sudo`.

This machine does have a TPM2 chip (`/dev/tpmrm0`, `NTC0702:00`, `tpm_tis`), so the passphrase prompt could also be removed by enrolling the TPM — a deliberate security trade-off, since the disk would then unlock on its own as long as it stays in this laptop.

See §9 for what was chosen and how it was implemented.

---

## 8. Scripts in this project

All under `scripts/`, runnable from `~/Work/fingerprint-reader/`:

| Script | What it does |
|---|---|
| `fp-status` | One-shot health check: package version and epoch, repo version, upgrade protection, `omarchy-pkg-missing` result, USB presence, fprintd state, device name, enrolled fingers, PAM wiring of all three stacks, boot-login path. Read-only. |
| `fp-restore-driver` | Reinstalls the kept `libfprint-git` epoch-2 package with `pacman -U --ask 4`, for the case where something did replace the driver. Needs sudo. |
| `fp-pam-test` | Runs one real PAM authentication against a named service (default `sddm`) and reports whether the fingerprint satisfied it, without restarting SDDM. Refuses any password prompt, so a pass can only mean the finger. Compiles `fp-pam-test.c`, needs sudo. |
| `fp-login-enable` | Wires `/etc/pam.d/sddm` for fingerprint and switches SDDM autologin off, with backups. Idempotent. Needs sudo. |
| `fp-login-disable` | Reverts both of those. Plain `sed`/`mv`, safe to run from a text console. |
| `fp-rebuild` | Rebuilds the package from `driver/PKGBUILD` with the required `BUILDDIR`/`PKGDEST` handling and prints the install command. Use only to move to a newer libfprint base. |

---

## 9. Login options (fingerprint at the greeter)

**Chosen on 2026-11-02: option A — fingerprint at the SDDM greeter**, keeping the LUKS passphrase. Boot becomes: passphrase → greeter (finger, or password) → desktop. That is one more interaction than before, because autologin asked for nothing.

Implemented by `scripts/fp-login-enable`, which makes exactly two changes and nothing else:

1. `/etc/pam.d/sddm` gets the same two lines omarchy puts in `sudo` and `polkit-1`, inserted before the existing `auth include system-login`:

   ```
   auth      [success=1 default=ignore] pam_exec.so quiet /usr/bin/omarchy-hw-laptop-closed
   auth      sufficient pam_fprintd.so
   ```

   `sufficient` is what keeps this safe: a reader that is missing, busy or unhappy falls through to `system-login`, so the password path is byte-for-byte the one that works today. The gate line skips the reader entirely when the lid is shut.

2. SDDM autologin is switched off — `/etc/sddm.conf.d/autologin.conf` is **renamed** to `.conf.disabled` (SDDM only reads `*.conf`), never deleted. A copy also goes to `backup/`, and the PAM file is backed up both to `/etc/pam.d/sddm.bak.<stamp>` and to `backup/`.

The script refuses to run if no finger is enrolled, restarts nothing, and leaves the running session alone; the change lands at the next boot or SDDM restart.

**How to test without a reboot:** switch to a text console (Ctrl+Alt+F2), log in with the password, then `sudo systemctl restart sddm` — that ends the graphical session, so save work first. At the greeter, touch the sensor; if the omarchy theme shows no fingerprint hint, press Enter on an empty password field and `pam_fprintd` takes the conversation from there. SDDM has no native fingerprint UI, so that prompt-then-touch sequence is normal.

**Revert:** `scripts/fp-login-disable` — strips the two lines and renames the autologin file back. It is deliberately plain `sed`/`mv` so it can be run from a text console if the greeter ever misbehaves. Recovery paths if something does go wrong: the password at the greeter (unchanged), a getty on Ctrl+Alt+F2 (`getty@.service` is enabled), and snapper snapshot #8 from the boot menu.

**If autologin ever comes back** after an `omarchy update`, re-run `scripts/fp-login-enable`; it is idempotent. `omarchy-refresh-sddm` only republishes the theme, so it does not touch autologin.

### 9.1 Correction, 2026-11-05 — why the first attempt did not work

After a reboot the finger still did nothing at login, and the journal said why:

```
sddm-helper[1146]: [PAM] Authenticating...
sddm-helper[1146]: pam_unix(sddm-autologin:session): session opened for user pc
```

The stack used was `sddm-autologin`, so autologin was still on and the greeter never appeared. Cause: the first version of `fp-login-enable` renamed `/etc/sddm.conf.d/autologin.conf` to `autologin.conf.disabled` **inside the same directory**, on the assumption that SDDM only reads `*.conf`. It does not. `sddm.conf(5)`:

> Configuration loads all files in the configuration directories followed by the configuration file in the order listed below with the latter having highest precedence.

No extension filter. A `.disabled` suffix changes nothing except the sort order, and since `a…` sorts after `99-…`, the renamed file was still read **last** and still won.

Fixed in both scripts:

* `fp-login-enable` now **moves the file out of the directory**, to `/etc/sddm.autologin.conf.disabled`, and detects autologin by parsing a non-empty `User=` under `[Autologin]` in *every* file of `/usr/lib/sddm/sddm.conf.d`, `/etc/sddm.conf.d` and `/etc/sddm.conf`, whatever the extension.
* `fp-login-disable` restores from that path, and still handles the old in-directory `.disabled` layout.
* `fp-status` had the same wrong assumption and reported autologin as OFF while it was on. It now parses all three locations the way SDDM does, names the file autologin comes from, prints which PAM stack the current boot actually used, and warns when `/etc/pam.d/sddm` is wired for fingerprint while autologin bypasses the greeter.

The PAM half of the first run was correct and is still in place, so only the autologin half needs re-running.

### 9.2 State after the re-run, and how it was verified (2026-11-05)

`fp-login-enable` was re-run with the fix. Checked without root:

* `/etc/sddm.conf.d/` now holds only `10-theme.conf`, `10-wayland.conf`, `99-omarchy-login.conf`; the autologin file sits outside the directory at `/etc/sddm.autologin.conf.disabled`, with copies in `backup/`. Parsing all three SDDM locations the way SDDM does (every file, last non-empty `User=` wins) finds no autologin user, so **the greeter will appear at the next boot**.
* `/etc/pam.d/sddm` has the clamshell gate and `auth sufficient pam_fprintd.so` ahead of `auth include system-login`, so fingerprint is tried first and the password path is untouched.
* `fprintd-list` still reports the sensor and `right-index-finger`.

What those checks cannot prove is that `pam_fprintd` actually succeeds inside the `sddm` service, and that the greeter's UI starts the attempt. `scripts/fp-pam-test` settles the first half: it runs a real `pam_authenticate()` against `/etc/pam.d/sddm` as root — exactly what `sddm-helper` does — and refuses any password prompt, so it can only pass on the finger. The second half is the greeter UI itself, and SDDM has no fingerprint UI (<https://github.com/sddm/sddm/issues/284>): PAM starts only once a login is submitted, which on the omarchy theme means pressing Enter with the password field empty. That part is only provable at a real greeter — reboot, or `systemctl restart sddm` from a text console.

**Not chosen:** leaving boot as it was, and TPM2 unlock of LUKS (`systemd-cryptenroll --tpm2-device=auto`), which would drop the passphrase prompt and leave the greeter as the only gate — weaker at-rest security, since a stolen laptop would unlock its own disk. The TPM2 chip is present (`/dev/tpmrm0`, `NTC0702:00`) if that trade is ever wanted.

---

## 10. Verification, revert, upkeep

**Verify** — `scripts/fp-status`, or by hand:

```bash
pacman -Q libfprint-git                              # expect 2:1.94.100.goodix538d.1-1
pacman -Qu libfprint-git                             # expect no output (no upgrade offered)
omarchy-pkg-missing libfprint-git fprintd usbutils   # expect exit 1 (all present)
fprintd-list "$USER"                                 # expect Goodix TLS Fingerprint Sensor 53XD + right-index-finger
fprintd-verify                                       # expect verify-match
```

**If the reader stops working after an update or a menu run:**

```bash
scripts/fp-restore-driver     # pacman -U the kept epoch-2 package
systemctl restart fprintd
fprintd-list "$USER"
```

**Full revert of the software side:** `omarchy remove security fingerprint` (clears the three PAM stacks, drops `fprintd`), then `sudo pacman -S libfprint-git` to take the repo version back, and `/run/media/pc/MY Drive/Backup/fingerprint-20261030-120432/pam.d-and-pacman.conf.tgz` if a PAM file needs restoring byte-for-byte. Snapper snapshot #8 predates everything. The sensor's pairing key is the one thing that cannot be reverted.

**Upkeep:** nothing routine. When you want a newer libfprint base, check `lbssousa/libfprint` for a newer tag, run `scripts/fp-rebuild`, refresh the PSK patch if it stops applying, and **keep `epoch` at 2 or higher** — that number is what holds the whole arrangement in place.

---

## 11. Related files

* `driver/` — PKGBUILD, both patches, the built package, and `driver/README.md` (build and upkeep detail). The old path `Projects/libfprint-goodix538d` is now a symlink here, so earlier references still resolve.
* `/run/media/pc/MY Drive/Docs/Notes/fingerprint-goodix-538d-notes.md` — the 2026-09-11 read-only investigation that stopped short of flashing, with the upstream bug links.
* Session transcript with every command and output: `~/.claude/projects/-home-pc/447aed88-88b6-4dd9-9098-252f91902b07.jsonl` (2026-10-30, 11:36–13:16 IST).

---

## 12. Documentation check (2026-11-05)

Asked whether fingerprint-at-login is a known Omarchy issue. It is not documented as one, and the greeter being skipped is intentional in Omarchy.

* The Omarchy manual's hardware-authentication page documents three contexts and no others: *"you'll be set to go using your fingerprint to unlock from the lock screen (which you can trigger with `Super + Escape`), enter sudo mode, and authorize system prompts."* It says nothing about SDDM, a greeter, a display manager, autologin, or LUKS. Both manual URLs (`…/77/fingerprint-fido2-authentication` and `…/77/hardware-authentication`) serve the same page. The only fallback it documents is `CTRL + C` at a `sudo` fingerprint prompt to get the password instead. For FIDO2 it states a limit — *"It's only for `sudo`, though, not unlocking your computer"* — and makes no equivalent statement about fingerprint.
* Omarchy issue #2880 ("Add a matching SDDM login screen theme") states the design intent plainly: *"While we never really see the SDDM login screen due to autologin…"*. So Omarchy ships autologin on purpose; fingerprint at the greeter is outside what it sets up, not a bug it tracks.
* Upstream, SDDM has no native fingerprint UI (<https://github.com/sddm/sddm/issues/284>), which is why the greeter shows a password field and `pam_fprintd` only gets the conversation once a login is submitted. Reports on other distributions describe exactly that: add the PAM line, press Enter on the empty field, touch the sensor, and the login goes through.

Sources: <https://learn.omacom.io/2/the-omarchy-manual/77/hardware-authentication>, <https://www.goodfirstissue.org/basecamp/omarchy/issues/2880>, <https://github.com/sddm/sddm/issues/284>.

## 13. System update on 2026-11-05

Omarchy went to `4.5.0-3`, kernel to `7.2.15-1-omarchy`, and the repo's `libfprint-git` moved to `1:1.94.100.r10.g6f9479c-5`. The epoch guard held: the installed driver is still `2:1.94.100.goodix538d.1-1`, `pacman -Qu libfprint-git` is silent, the sensor is still detected, and `right-index-finger` is still enrolled. Fingerprint for `sudo`, polkit and the lock screen was unaffected by the update.
