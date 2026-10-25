# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

# proot-termux as a static aarch64-linux musl binary, cross-compiled from
# x86_64-linux with nixpkgs' gcc cross toolchain. The bootstrap ships it and
# on-device generations refer to the same store path, so both take it from here.
{ nixpkgs }:

let
  pkgs = (import nixpkgs { system = "x86_64-linux"; }).pkgsCross.aarch64-multiplatform-musl.pkgsStatic;

  # What termux's proot gets from bionic (and glibc) but not from musl.
  muslCompat = pkgs.writeText "musl-compat.h" ''
    #ifndef __ASSEMBLER__
    #include <errno.h>
    #include <sys/ioctl.h>
    #define MSG_COPY 040000
    #ifndef TCGETS2 /* asm-generic, sizeof(struct termios2) == 44 */
    #define TCGETS2 _IOR('T', 0x2A, char[44])
    #define TCSETS2 _IOW('T', 0x2B, char[44])
    #define TCSETSW2 _IOW('T', 0x2C, char[44])
    #define TCSETSF2 _IOW('T', 0x2D, char[44])
    #endif
    #define TEMP_FAILURE_RETRY(exp) ({ long _rc; \
      do _rc = (long) (exp); while (_rc == -1 && errno == EINTR); _rc; })
    #endif
  '';
in

(pkgs.callPackage ./proot-termux {
  talloc = pkgs.callPackage ./talloc-static.nix { };
}).overrideAttrs (old: {
  postPatch = (old.postPatch or "") + ''
    # musl < 1.2.5 has no struct statx, take it from the kernel headers.
    sed -i '/#include "sys\/vfs.h"/a #include <linux/stat.h>' src/tracee/statx.h
    # An lld option, GNU ld does not know it (upstream proot does not use it).
    substituteInPlace src/GNUmakefile --replace-fail ',--rosegment' ""
  '';
  # proot's Android code paths (e.g. rewriting TCGETS2 ioctls, which the app
  # SELinux policy denies) are keyed on the bionic compiler's __ANDROID__.
  CFLAGS = old.CFLAGS ++ [ "-D__ANDROID__" "-include" "${muslCompat}" ];
  # musl's fortify-headers trap (SIGTRAP) in proot's execve handling, which
  # kills the tracer and leaves the tracee with ENOSYS for every syscall.
  hardeningDisable = [ "fortify" "fortify3" ];
})
