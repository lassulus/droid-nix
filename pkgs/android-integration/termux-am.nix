# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ stdenv, fetchFromGitHub, cmake, androidApp }:

let
  # The app's am server (app/src/main/java/us/lassul/droidnix/AmServer.java).
  socketPath = "${androidApp.dataDir}/files/am.sock";
in
stdenv.mkDerivation rec {
  name = "termux-am";
  version = "1.5.0";
  src = fetchFromGitHub {
    owner = "termux";
    repo = "termux-am-socket";
    rev = version;
    sha256 = "sha256-6pCv2HMBRp8Hi56b43mQqnaFaI7y5DfhS9gScANwg2I=";
  };
  nativeBuildInputs = [ cmake ];
  patchPhase = ''
    # Header generation doesn't seem to work on android
    echo "#define SOCKET_PATH \"${socketPath}\"" > termux-am.h
    # Fix the bash link so that nix can patch it + path to termux-am-socket
    substituteInPlace termux-am.sh.in \
      --replace-quiet @TERMUX_PREFIX@/bin/bash /bin/bash \
      --replace-quiet \
        "termux-am-socket \"\$am_command_string\"" \
        "$out/bin/termux-am-socket \"\$am_command_string\""
  '';
  postInstall = ''
    # Scripts use 'am' as an alias.
    ln -s $out/bin/termux-am $out/bin/am
  '';
}
