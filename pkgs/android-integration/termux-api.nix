# The Termux:API commands (termux-notification, termux-location, ...): the
# termux-api helper broadcasts each call through termux-am to the receiver
# that the droid-nix app carries (app/termux-api-patches).
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
}
