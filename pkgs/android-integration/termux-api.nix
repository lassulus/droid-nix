# The Termux:API commands, installed as droid-nix-notification,
# droid-nix-location, ...: the droid-nix-api helper broadcasts each call
# through droid-nix-am to the receiver that the droid-nix app carries
# (app/termux-api-patches).
{ stdenv, fetchFromGitHub, cmake, getopt, gnused, termux-am, androidApp }:

stdenv.mkDerivation rec {
  name = "termux-api";
  version = "0.60.0";
  src = fetchFromGitHub {
    owner = "termux";
    repo = "termux-api-package";
    rev = "v${version}";
    hash = "sha256-2yIJAeg+WYJwZSHzEKCKwDMZFtsVOQh2zi5PN821VAQ=";
  };
  nativeBuildInputs = [ cmake ];
  # cmake_minimum_required(VERSION 3.0.0)
  cmakeFlags = [ "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" ];

  postPatch = ''
    # The droid-nix-* names, before the store paths below go in: the
    # scripts' help texts, the calls of each other and of the libexec
    # helpers (renamed in postInstall), and termux-usb's environment
    # variables.
    sed -i 's|termux-|droid-nix-|g' scripts/*.in
    substituteInPlace termux-api.c --replace-fail termux-callback droid-nix-callback
    substituteInPlace termux-api.c termux-callback.in scripts/termux-usb.in \
      --replace-quiet TERMUX_CALLBACK DROID_NIX_CALLBACK \
      --replace-quiet TERMUX_EXPORT_FD DROID_NIX_EXPORT_FD \
      --replace-quiet TERMUX_USB_FD DROID_NIX_USB_FD

    substituteInPlace termux-api.c \
      --replace-fail '"/data/data/com.termux/files/usr"' "\"$out\"" \
      --replace-fail 'execv(PREFIX "/bin/am"' 'execv("${termux-am}/bin/am"' \
      --replace-fail '"com.termux.api/.TermuxApiReceiver"' \
        '"${androidApp.packageName}/com.termux.api.TermuxApiReceiver"'
    substituteInPlace scripts/termux-api-start.in scripts/termux-api-stop.in \
      --replace-fail 'am ' '${termux-am}/bin/am ' \
      --replace-fail com.termux.api/.KeepAliveService \
        ${androidApp.packageName}/com.termux.api.KeepAliveService
    substituteInPlace scripts/termux-notification.in scripts/termux-job-scheduler.in \
      --replace-fail 'TEMP=`getopt ' 'TEMP=`${getopt}/bin/getopt '
    substituteInPlace scripts/termux-media-scan.in \
      --replace-fail '| sed ' '| ${gnused}/bin/sed '
    substituteInPlace scripts/termux-sms-list.in \
      --replace-fail '        sed \' '        ${gnused}/bin/sed \'
    # patchShebangs points these to the store
    sed -i 's|^#!@TERMUX_PREFIX@/bin/|#!/bin/|' scripts/*.in termux-callback.in
  '';

  postInstall = ''
    for f in $out/bin/termux-*; do
      mv $f $out/bin/droid-nix-''${f#$out/bin/termux-}
    done
    mv $out/libexec/termux-api-broadcast $out/libexec/droid-nix-api
    mv $out/libexec/termux-callback $out/libexec/droid-nix-callback
    rm $out/libexec/termux-api
    # The C library of the helper; nothing in droid-nix links it.
    rm -r $out/lib $out/include
  '';
}
