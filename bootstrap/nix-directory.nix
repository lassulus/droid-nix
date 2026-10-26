# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ stdenvNoCC
, closureInfo
, launcher
, nix
, system
}:

let
  buildRootDirectory = "root-directory";

  launcherClosure = closureInfo {
    rootPaths = [
      launcher
    ];
  };
in

stdenvNoCC.mkDerivation {
  name = "nix-directory";

  src = builtins.fetchurl {
    url = "https://nixos.org/releases/nix/nix-2.20.5/nix-2.20.5-${system}.tar.xz";
    sha256 =
      let
        nixShas = {
          aarch64-linux = "sha256:168wjfj3xsc8hq1y6cq59iipjp1g9hmj4n5wdn9c47ad9gbc9cvh";
        };
      in
      nixShas.${system};
  };

  buildPhase = ''
    mkdir --parents ${buildRootDirectory}/nix
    cp --recursive store ${buildRootDirectory}/nix/store

    CACERT=$(find ${buildRootDirectory}/nix/store -path '*-nss-cacert-*/ca-bundle.crt' | sed 's,^${buildRootDirectory},,')
    PKG_BASH=$(find ${buildRootDirectory}/nix/store -path '*/bin/bash' | sed 's,^${buildRootDirectory},,')
    PKG_BASH=''${PKG_BASH%/bin/bash}
    PKG_NIX=$(find ${buildRootDirectory}/nix/store -path '*/bin/nix' | sed 's,^${buildRootDirectory},,')
    PKG_NIX=''${PKG_NIX%/bin/nix}

    for i in $(< ${launcherClosure}/store-paths); do
      cp --archive "$i" "${buildRootDirectory}$i"
    done

    # Register the paths in a store with store dir /nix/store whose files and
    # state live under ${buildRootDirectory} (a chroot store).
    export HOME=$TMPDIR
    ${nix}/bin/nix-store --store "$PWD/${buildRootDirectory}" --load-db < .reginfo
    ${nix}/bin/nix-store --store "$PWD/${buildRootDirectory}" --load-db < ${launcherClosure}/registration

    cat > package-info.nix <<EOF
    {
      bash = "$PKG_BASH";
      cacert = "$CACERT";
      nix = "$PKG_NIX";
    }
    EOF
  '';

  installPhase = ''
    mkdir $out
    cp --recursive ${buildRootDirectory}/nix/store $out/store
    cp --recursive ${buildRootDirectory}/nix/var $out/var
    install -D -m 0644 package-info.nix $out/nix-support/package-info.nix
  '';

  fixupPhase = "true";
}
