{ stdenv
, pkg-config
, wayland
, wayland-scanner
, wlr-protocols
, wlroots
, libxkbcommon
}:

# Serves a wlroots compositor to the app's DesktopActivity, see server.c.
stdenv.mkDerivation {
  name = "droid-nix-desktop-server";
  src = ./server.c;
  dontUnpack = true;
  strictDeps = true;
  nativeBuildInputs = [ pkg-config wayland-scanner ];
  buildInputs = [ wayland libxkbcommon ];
  buildPhase = ''
    runHook preBuild
    for xml in ${wlr-protocols}/share/wlr-protocols/unstable/wlr-{screencopy,virtual-pointer,output-management}-unstable-v1.xml \
        ${wlroots.src}/protocol/virtual-keyboard-unstable-v1.xml; do
      name=$(basename $xml .xml)
      wayland-scanner client-header $xml $name-client-protocol.h
      wayland-scanner private-code $xml $name-protocol.c
    done
    $CC -O2 -Wall -Wextra -Werror -Wno-unused-parameter -I. -o droid-nix-desktop-server $src *-protocol.c \
      $($PKG_CONFIG --cflags --libs wayland-client xkbcommon)
    runHook postBuild
  '';
  installPhase = ''
    runHook preInstall
    install -D droid-nix-desktop-server $out/bin/droid-nix-desktop-server
    runHook postInstall
  '';
}
