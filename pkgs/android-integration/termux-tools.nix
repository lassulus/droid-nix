# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ stdenvNoCC
, fetchFromGitHub
, autoreconfHook
, makeWrapper
, gnused
, getopt
, termux-am
, androidApp
}:

stdenvNoCC.mkDerivation rec {
  name = "termux-tools";
  version = "1.42.4";
  src = fetchFromGitHub {
    owner = "termux";
    repo = "termux-tools";
    rev = "v${version}";
    sha256 = "sha256-LkkeaEQcY8HgunBYAg3Ymn5xYPvrGqGNCZTd/NyIOKY=";
  };
  nativeBuildInputs = [ autoreconfHook makeWrapper ];
  propagatedInputs = [ termux-am ];

  patches = [
    # https://github.com/termux/termux-tools/pull/95
    ./termux-tools.patch
    ./termux-tools-help.patch
  ];
  postPatch = ''
    substituteInPlace scripts/termux-setup-storage.in \
      --replace-quiet @TERMUX_HOME@ ${androidApp.dataDir}/files/home/ \
      --replace-quiet @TERMUX_APP_PACKAGE@ ${androidApp.packageName}
    substituteInPlace scripts/termux-open.in \
      --replace-quiet 'getopt ' '${getopt}/bin/getopt '
    substituteInPlace scripts/termux-open.in \
      --replace-fail @TERMUX_APP_PACKAGE@.app.TermuxOpenReceiver @TERMUX_APP_PACKAGE@.OpenReceiver
    substituteInPlace scripts/termux-wake-lock.in scripts/termux-wake-unlock.in \
      --replace-fail @TERMUX_APP_PACKAGE@.app.TermuxService @TERMUX_APP_PACKAGE@.TerminalService
    substituteInPlace \
      scripts/termux-open.in \
      scripts/termux-wake-lock.in \
      scripts/termux-wake-unlock.in \
      --replace-quiet @TERMUX_APP_PACKAGE@ ${androidApp.packageName}
    substituteInPlace scripts/termux-reload-settings.in \
      --replace-quiet @TERMUX_APP_PACKAGE@ ${androidApp.packageName}
    ${gnused}/bin/sed -i 's|^am |${termux-am}/bin/am |' scripts/*

    rm -r doc  # manpage is half misleading, pulling pandoc is not worth it
    substituteInPlace Makefile.am --replace-quiet \
      'SUBDIRS = . scripts doc mirrors motds' \
      'SUBDIRS = . scripts'
    substituteInPlace configure.ac --replace-quiet \
      'AC_CONFIG_FILES([Makefile scripts/Makefile doc/Makefile' \
      'AC_CONFIG_FILES([Makefile scripts/Makefile])'
    substituteInPlace configure.ac --replace-quiet \
      'mirrors/Makefile motds/Makefile])' ""
  '';

  outputs = [
    "out" # all the unsupported unsorted stuff
    "setup_storage" # droid-nix-setup-storage
    "open" # droid-nix-open
    "open_url" # droid-nix-open-url
    "reload_settings" # droid-nix-reload-settings
    "wake_lock" # droid-nix-wake-lock
    "wake_unlock" # droid-nix-wake-unlock
    "xdg_open" # xdg-open
  ];
  postInstall = ''
    rm $out/etc/termux-login.sh
    rm $out/etc/profile.d/init-termux-properties.sh
    rm -d $out/etc/profile.d
    rm -d $out/etc

    rm $out/bin/chsh      # we offer a declarative way to change your shell
    rm $out/bin/cmd       # doesn't work because we overlay /system/bin
    rm $out/bin/dalvikvm  # doesn't work because we overlay /system/bin
    rm $out/bin/df        # works without the magic
    rm $out/bin/getprop   # doesn't work because we overlay /system/bin
    rm $out/bin/logcat    # doesn't work because we overlay /system/bin
    rm $out/bin/login     # we have our own, very complex login
    rm $out/bin/ping      # doesn't work because we overlay /system/bin
    rm $out/bin/ping6     # doesn't work because we overlay /system/bin
    rm $out/bin/pkg       # we use Nix
    rm $out/bin/pm        # doesn't work because we overlay /system/bin
    rm $out/bin/settings  # doesn't work because we overlay /system/bin
    rm $out/bin/su        # doesn't work because we overlay /bin
    rm $out/bin/top       # doesn't work because we overlay /system/bin

    rm $out/bin/termux-change-repo            # we use Nix
    rm $out/bin/termux-fix-shebang            # we use Nix
    rm $out/bin/termux-info                   # Termux-specific. write our own?
    rm $out/bin/termux-reset                  # untested and dangerous
    rm $out/bin/termux-restore                # untested and dangerous
    rm $out/bin/termux-setup-package-manager  # we use Nix

    mkdir -p $setup_storage/bin
    mv $out/bin/termux-setup-storage $setup_storage/bin/droid-nix-setup-storage

    mkdir -p $open/bin
    mv $out/bin/termux-open $open/bin/droid-nix-open

    mkdir -p $open_url/bin
    mv $out/bin/termux-open-url $open_url/bin/droid-nix-open-url

    mkdir -p $reload_settings/bin
    mv $out/bin/termux-reload-settings $reload_settings/bin/droid-nix-reload-settings

    mkdir -p $wake_lock/bin
    mv $out/bin/termux-wake-lock $wake_lock/bin/droid-nix-wake-lock

    mkdir -p $wake_unlock/bin
    mv $out/bin/termux-wake-unlock $wake_unlock/bin/droid-nix-wake-unlock

    mkdir -p $xdg_open/bin
    rm $out/bin/xdg-open
    ln -s $open/bin/droid-nix-open $xdg_open/bin/xdg-open

    mv $out/bin/termux-backup $out/bin/droid-nix-backup

    # check that we didn't package we didn't want to
    find $out | ${gnused}/bin/sed "s|^$out|.|" | sort > effective
    echo . >> expected
    echo ./bin >> expected
    echo ./bin/droid-nix-backup >> expected        # entirely untested
    echo ./share >> expected
    echo ./share/examples >> expected
    echo ./share/examples/termux >> expected
    echo ./share/examples/termux/termux.properties >> expected  # useful
    diff -u expected effective
  '';
}
