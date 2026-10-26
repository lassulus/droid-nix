# In-process /nix interception on Android: phase 4 (issue #1)

Phase 3 ([RESULTS-phase3.md](RESULTS-phase3.md)) ran gcc and a small
`nix-build` under the launcher, but only `/nix` was translated. Phase 4 does
items 1 and 2 of its "Recommended next step", in place in `loader.c`. The
launcher now has all of proot's binds. The native nix-build needs no
`NIX_CONF_DIR`/`TMPDIR` setup, DNS works, and GNU hello builds from source
with nixpkgs' stdenv, 3.9× faster than under proot.

Device and setup are the same as in phase 3: Pixel 8a, Android 16, kernel
6.1.145, app `us.lassul.droidnix` (`untrusted_app_27`), date 2026-10-26.
Native runs used the failsafe session (`Seccomp_filters 1`, `TracerPid 0`).
proot runs used the normal droid-nix shell (`Seccomp_filters 2`, traced by
proot).

## Binds

The launcher takes proot's bind syntax, `-b host:guest`, before the target:

```
loader [-b host:guest]... TARGET [argv0 argv1 ...]
```

The binds form one prefix table that `canon()` uses for every path. A
re-exec gets the canonical table on its command line, after `--reexec`. nix
clears the builder environment, so the table cannot travel in an env var.
The built-in `/nix` prefix and `SUD_PREFIX` are gone. `/nix` is a bind like
the others.

`binds.sh` builds the list. **It mirrors the proot command line in
`modules/environment/login/login.nix`**: the same binds in the same order,
and the same `[ ! -r /proc/stat ] && [ -e fake ]` conditions for the two
`/proc` fakes. The two lists can later come from one place, because the
launcher accepts the same options:

| bind (login.nix) | host on this device |
| --- | --- |
| `-b <installationDir>/nix:/nix` | `…/files/usr/nix` |
| `-b <installationDir>/bin:/bin!` | `…/files/usr/bin` (`sh` → bash-interactive) |
| `-b <installationDir>/etc:/etc!` | `…/files/usr/etc` (resolv.conf, passwd, nix/nix.conf, ssl) |
| `-b <installationDir>/tmp:/tmp` | `…/files/usr/tmp` |
| `-b <installationDir>/usr:/usr` | `…/files/usr/usr` |
| `-b <installationDir>/dev/shm:/dev/shm` | `…/files/usr/dev/shm` |
| `-b <fakeProcStat>:/proc/stat` (if `/proc/stat` is unreadable) | `…-fakeProcStat` store file (`btime 0`) |
| `-b <fakeProcUptime>:/proc/uptime` (if `/proc/uptime` is unreadable) | `…-fakeProcUptime` store file |
| `-b /:/android` | `/` |

`<installationDir>` is `/data/data/us.lassul.droidnix/files/usr`. Both
`/proc` files are unreadable in the app domain, so both fakes are active, as
in the running proot.

### Semantics, taken from proot's source (`path/binding.c`, `path/path.c`, `syscall/enter.c`)

| what | proot | launcher |
| --- | --- | --- |
| host path | `realpath`, must exist | the same, once, in the first process |
| guest path | canonicalised in the guest view of the binds it is nested in, last component followed unless it ends in `!` | the same: containers first, `!` stripped |
| guest → host | longest guest prefix (`substitute_binding(GUEST)`), rootfs `/` otherwise | `to_host()`, the same |
| host → guest | longest host prefix (`substitute_binding(HOST)`). Among equal host paths the rootfs `/:/` comes first, so it wins over `/:/android` | `to_guest()`, the same (host `/` never wins) |
| getcwd | the guest cwd, detranslated from the host path at chdir | reverse-translates the kernel's cwd. Same result |
| readlink results | reverse-translated only for links under `/proc` (`detranslate_path` with a referrer; `belongs_to_guestfs` is always true with rootfs `/`) | the same. Phase 3 reverse-translated every result |
| relative paths | always resolved against the guest cwd | resolved by the kernel only if no component is `..`, a followed symlink or **the last name of a bind's guest path** (the only way to step into a bind). Otherwise the full walk |

The last row fixes a phase 3 bug as well: from cwd `/`, `ls nix/store` gave
ENOENT, because the kernel resolved the relative `nix` on the host.

Not supported, with a clear error: a relative guest path, binding the guest
root `/` (proot's `-r`), and two binds with the same guest path or the same
host path other than `/`. proot resolves the last two by its list order.

The `!` on `/bin` and `/etc` matters on Android. Without it, proot (and the
launcher) would follow the guest `/bin` → `/system/bin` and bind there.
`/tmp` is a real directory on Android 16. `/usr` and `/dev/shm` do not exist.
So the canonicalisation is a no-op for this table, but it is implemented as
proot does it.

## Correctness

### Bind semantics: launcher vs proot

`binds-check.sh` runs with the same bash under the launcher and under proot.
It covers `/etc` (resolv.conf, nix.conf, the `static` chain into the store),
`/bin/sh`, `/usr/lib`, the `/proc/stat` and `/proc/uptime` fakes, files
created in `/tmp` and `/dev/shm` and seen at their host paths, `/android` as
the host root, `realpath` through binds, `..` across bind edges, absolute,
relative and host-path symlinks inside a bind, `pwd -P` and
`readlink /proc/self/cwd` in nine directories, relative paths that step into
a bind from `/`, `/dev`, `/proc` and `/tmp`, `find` from a cwd in a bind, and
`readlink /proc/self/fd/N`.

The two outputs (125 lines) are **identical**. The first attempt
differed only in the order of one `ls` listing: the proot session has
`LOCALE_ARCHIVE` (en_US collation), the failsafe does not. The check now
sets `LC_ALL=C`.

### DNS (native, `dns-native.sh`)

| case | result |
| --- | --- |
| `nix-build <nixpkgs> -A getent` (substitutes 2 paths from cache.nixos.org) | exit 0 |
| `getent hosts cache.nixos.org github.com` | fastly IPv6 addresses for cache.nixos.org, `140.82.121.3 github.com`, exit 0 |
| `getent ahostsv4 example.org` | `104.20.26.136`, exit 0 |
| `getent passwd droid-nix` | the droid-nix entry from the bound `/etc/passwd` |
| the same `getent hosts`, launcher with only the `/nix` bind (the phase 3 view) | exit 2 (no `/etc/resolv.conf`) |
| `nix path-info --refresh --store https://cache.nixos.org <hello>` | found, exit 0 |

glibc reads `/etc/resolv.conf` and `/etc/hosts` by absolute path. Only the
`/etc` bind makes them exist. droid-nix's `/etc` has no `nsswitch.conf`, so
glibc's built-in default order applies, as under proot.

### Phase 3 checks (`run-native.sh`)

All phase 3 cases give the same results as in phase 3: hello, bash
pipelines, `ls`, `nix --version`, `verify-path`, `/proc/self/exe`, the gcc
driver and cc1 (ET_EXEC), a gcc-wrapper compile and link and its program
(exit 42), the path canonicalisation set, execvp's ENOENT, the
symlink/rename/unlink set and the hard link EACCES. New:
`cd /; ls -d nix/store tmp` works (phase 3: ENOENT).

## GNU hello from source (`hello.nix`)

`hello.nix` is nixpkgs' `hello` with a nonce, from the nixpkgs-unstable
source already in the device store (26.11, rev 674a2028). It is a full
stdenv build: unpack, `updateAutotoolsGnuConfigScripts`, `configure`,
`make -j9`, `make check`, `make install`, fixup (patchelf, strip, gzip man
pages, patchShebangs) and `installCheck` (versionCheckHook).

* **The first build ran under the launcher, with substitution on.** The
  rest of stdenv's closure was already in the store. nix substituted the
  three missing paths (`stdenv-linux`, `version-check-hook`, the
  `hello-2.12.3.tar.gz` source) from cache.nixos.org and built hello in
  about 2 minutes (from the file times; see the mksh note below). No proot
  fallback was needed for fetching.
* **No new app-domain issue showed up.** The phase 2/3 emulations
  (set_robust_list/rseq, clone3, termios2, SIGSYS defence) were enough.
  `make check`: 6 PASS, 1 SKIP (`atexit-1`, skipped the same way under
  proot), 0 FAIL.
* **No hard links.** One extra native build ran under a static strace
  (`hello-strace.sh`, `-f -e trace=linkat,symlinkat`). It made **0 linkat
  calls**, nix included, and 26 symlinkat calls (configure's `ln -s`
  probes). Nothing failed with EACCES, so link2symlink emulation is not
  needed for this build and was left out.
* **Same configure results as proot.** All 268 `checking ...` lines of the
  native and the proot log are identical, except the build directory name.

### Timing: launcher vs proot

3 runs each, fresh output each run (new nonce), `--option substitute false`
after stdenv and the source were in the store, timed as in phase 3.
`hello-native.sh` (failsafe) and `hello-proot.sh` (proot shell).

| workload | launcher (ms) | proot (ms) | speedup |
| --- | ---: | ---: | ---: |
| **`nix-build` of GNU hello (stdenv, configure/make/check)** | **89644** | **352537** | **3.9×** |

Raw runs (ms):

* launcher: 89555/89761/89644
* proot: 354468/350405/352537

The failsafe's `/system/bin/sh` is mksh. Its `$(( ))` is 32-bit, so
nanosecond differences wrap after 4.29 s. The hello scripts compute ms from
whole seconds plus the millisecond digits. The phase 3 timings were all
below 4.29 s, so they are unaffected.

## Regression check: phase 3 benchmarks

Same workloads and timing as phase 3 (3 runs, median). The native nix-build
now runs without `NIX_CONF_DIR`/`TMPDIR`.

| workload | launcher (ms) | proot (ms) | speedup | phase 3 launcher |
| --- | ---: | ---: | ---: | ---: |
| `nix-store --verify-path` | 188 | 973 | 5.2× | 195 |
| `find` over the subtree | 62 | 387 | 6.2× | 53 |
| `du -s` | 63 | 1438 | 23× | 67 |
| `tar cf /dev/null` | 64 | 1043 | 16× | 66 |
| small gcc compile + link | 432 | 1546 | 3.6× | 433 |
| `nix-build` of `build.nix` | 632 | 2430 | 3.8× | 654 |

Raw runs (ms):

* launcher: verify 187/196/188, find 64/62/60, du 63/62/64, tar 71/64/60, gcc 445/432/421
* launcher nix-build: 615/632/655
* proot: verify 973/1116/967, find 387/394/302, du 756/1438/1596, tar 1043/990/1585, gcc 1524/1546/1546
* proot nix-build: 2074/2430/2613

find looked ~8 ms slower than phase 3, so the phase 3 and phase 4 loaders
ran interleaved in one session, 5 runs each (`p3`: only `/nix`; `p4`: the
binds):

| workload | phase 3 loader median | phase 4 loader median |
| --- | ---: | ---: |
| verify | 193 (219/174/193/193/188) | 194 (188/194/201/200/190) |
| find | 57 (51/57/57/58/58) | 55 (55/55/60/55/54) |
| du | 61 (61/60/63/63/61) | 62 (60/72/71/62/58) |
| tar | 65 (65/64/68/66/64) | 67 (67/67/71/65/62) |

**No regression.** The difference from the phase 3 table is run-to-run
noise. The bind table adds a scan of 9 entries per resolved component and a
name check per relative component. That is small next to the trapped
syscall.

## Limitations (current)

* **No `--sysvipc`.** proot's SysV IPC emulation is not there. How SysV
  IPC syscalls behave under the launcher in the app domain was not tested.
  hello did not use them.
* **Hard links are EACCES** (Android policy). There is no link2symlink
  emulation. hello's build makes no hard links (see above), so it was left
  out.
* **A dirfd or cwd on a host directory hidden by a bind** (only reachable as
  `/android/tmp`, `/android/etc/…` etc.) is not handled as proot handles
  it. proot detranslates such a directory statelessly to the bound guest
  path (`/tmp`). The launcher's fast path keeps resolving relative names in
  the real directory, while its slow path and getcwd use the bound one.
  Such a directory can only be reached through `/android`.
* **The bind list has to be given to the launcher.** `binds.sh` mirrors
  `login.nix` by hand, including the device's fake `/proc` store paths.
* The phase 3 limitations remain: clone3 → ENOSYS, a program's own SIGSYS
  use, the fixed loader address, unix datagram sends to a path, bpffs,
  openat2 with RESOLVE_* translated lexically, `/proc/self/exe` answered for
  readlinkat only, termios2 BOTHER speeds, a deleted dirfd's name.

## What this means for issue #1

* **The launcher now gives the same view as droid-nix's proot.** It has the
  same nine binds with proot's semantics, checked side by side with proot.
  With it, nix needs no extra environment, glibc finds DNS and the user
  database, and `/bin/sh` is droid-nix's bash.
* **A real stdenv build works with no proot at all**, from substitution
  through configure, make, the test suite, install and fixup, and it is
  3.9× faster than under proot (89644 vs 352537 ms). It needed no
  new app-domain emulation and no link2symlink.
* The bind table costs nothing measurable on the store traversals.

## Recommended next step

1. Try the launcher as droid-nix's login path behind a switch, with proot
   kept as the fallback (phase 3's step 3). Generate the launcher's `-b`
   list from the same Nix list as proot's command line in `login.nix`, so
   the binds and the `/proc` fake conditions have one source.
2. Before that, run a build that is known to make hard links or use SysV
   IPC (e.g. one with `cp -l`/`ln` in its install or a test suite that uses
   shm). Only then decide on link2symlink and `--sysvipc` emulation.
