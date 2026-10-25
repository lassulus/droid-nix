{
  description = "droid-nix: a Termux-based terminal app with Nix for Android";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Builds the bootstrap zip (cross-compiled proot, nix 2.20.5). Pinned like
    # upstream nix-on-droid (head of nixos-24.05 as of 2024-07-06); when bumping
    # it, update the proot-static path in modules/environment/login/default.nix.
    nixpkgs-for-bootstrap.url = "github:NixOS/nixpkgs/49ee0e94463abada1de470c9c07bfc12b36dcf40";

    # Upstream termux-app; app/patches is applied on top of it.
    termux-app = {
      url = "github:termux/termux-app/8629e632fcb95da272221be327db653fb24befe9";
      flake = false;
    };
  };

  outputs =
    { self, nixpkgs, ... }@inputs:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config = {
          # The Android SDK is unfree; accepting its license is part of this expression.
          android_sdk.accept_license = true;
          allowUnfree = true;
        };
      };
      android = pkgs.callPackage ./pkgs/android-sdk.nix { };
      bootstrap = import ./bootstrap {
        nixpkgs = inputs.nixpkgs-for-bootstrap;
        nativeSystem = system;
      };
    in
    {
      packages.${system} = {
        droid-nix-app = pkgs.callPackage ./app {
          inherit android;
          inherit (inputs) termux-app;
        };
        # Directory with bootstrap-aarch64.zip, cross-compiled on x86_64-linux.
        bootstrap-aarch64 = bootstrap.bootstrapZip;
        sign = pkgs.callPackage ./pkgs/sign.nix { inherit android; };
        default = self.packages.${system}.droid-nix-app;
      };

      # The switch tool, run on the device by the first login and by users.
      packages.aarch64-linux = rec {
        droid-nix = nixpkgs.legacyPackages.aarch64-linux.callPackage ./pkgs/droid-nix { };
        default = droid-nix;
      };

      apps.${system} = {
        sign = {
          type = "app";
          program = "${self.packages.${system}.sign}/bin/sign";
          meta.description = "Sign an APK with a keystore given via SIGN_* env vars";
        };
        # Regenerates app/deps.json (needs network); run from the flake root.
        update-deps = {
          type = "app";
          program = "${self.packages.${system}.droid-nix-app.mitmCache.updateScript}";
          meta.description = "Regenerate the gradle deps.json lockfile of the app";
        };
      };

      # On-device configuration, see templates/default.
      lib.droidNixConfiguration =
        {
          pkgs,
          modules ? [ ],
          extraSpecialArgs ? { },
          home-manager-path ? null,
        }:
        if pkgs.stdenv.hostPlatform.system != "aarch64-linux" then
          throw "droid-nix only supports aarch64-linux, got ${pkgs.stdenv.hostPlatform.system}"
        else
          import ./modules {
            inherit pkgs extraSpecialArgs home-manager-path;
            config.imports = modules;
          };

      templates.default = {
        path = ./templates/default;
        description = "droid-nix on-device configuration";
      };

      # Evaluates the template configuration. uid/gid are detected by building
      # on the device and the pinned proot path needs impure evaluation, so the
      # check sets them (proot to the same cross-built derivation).
      checks.${system}.template = pkgs.writeText "droid-nix-template-drv" (builtins.unsafeDiscardOutputDependency
        (self.lib.droidNixConfiguration {
          pkgs = nixpkgs.legacyPackages.aarch64-linux;
          modules = [
            ./templates/default/droid-nix.nix
            {
              user.uid = 10000;
              user.gid = 10000;
              environment.files.prootStatic = bootstrap.prootTermux;
            }
          ];
        }).activationPackage.drvPath
      );
    };
}
