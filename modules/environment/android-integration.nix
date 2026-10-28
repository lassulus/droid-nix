# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

{ config, lib, pkgs, androidApp, ... }:

let
  cfg = config.android-integration;

  termux-am =
    pkgs.callPackage ../../pkgs/android-integration/termux-am.nix { inherit androidApp; };
  termux-tools =
    pkgs.callPackage ../../pkgs/android-integration/termux-tools.nix {
      inherit androidApp termux-am;
    };
  termux-api =
    pkgs.callPackage ../../pkgs/android-integration/termux-api.nix {
      inherit androidApp termux-am;
    };
in
{

  ###### interface

  options.android-integration = {

    am.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide an `am` (activity manager) command.
        Is not guaranteed to be a real deal, could be of limited compatibility
        with real `am` (also installed as `droid-nix-am`).
      '';
    };

    api.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide the Termux:API commands as
        `droid-nix-notification`, `droid-nix-location`, `droid-nix-clipboard-get`, ...
        that use Android APIs through the droid-nix app.
      '';
    };

    open.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide a `droid-nix-open` command
        that opens files or urls in external apps
        (through the droid-nix app).
      '';
    };

    open-url.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide a `droid-nix-open-url` command
        that opens files or urls in external apps
        (uses `android.intent.action.VIEW`).
      '';
    };

    setup-storage.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide a `droid-nix-setup-storage` command
        that makes the app request storage permission,
        and then creates a $HOME/storage directory with symlinks to storage.
      '';
    };

    reload-settings.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide a `droid-nix-reload-settings` command
        which applies changes to font, colorscheme or terminal
        without the need to close all the sessions.
      '';
    };

    wake-lock.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide a `droid-nix-wake-lock` command
        that tones down Android power saving measures.
        This is the same action that's available from the notification.
      '';
    };

    wake-unlock.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide a `droid-nix-wake-unlock` command
        that undoes the effect of the `droid-nix-wake-lock` one.
      '';
    };

    wakeLock.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Acquire the wake lock (like `droid-nix-wake-lock`) at every login and
        switch, so that programs keep running with the screen off.
        `services.sshd` turns this on by default.
      '';
    };

    xdg-open.enable = lib.mkOption {
      type = lib.types.bool;
      default = false;
      example = "true";
      description = ''
        Provide an `xdg-open` alias to `droid-nix-open` command.
      '';
    };

  };

  ###### implementation

  config = let ifD = cond: pkg: if cond then [ pkg ] else [ ]; in {
    environment.packages =
      (ifD cfg.am.enable termux-am) ++
      (ifD cfg.api.enable termux-api) ++
      (ifD cfg.setup-storage.enable termux-tools.setup_storage) ++
      (ifD cfg.open.enable termux-tools.open) ++
      (ifD cfg.open-url.enable termux-tools.open_url) ++
      (ifD cfg.reload-settings.enable termux-tools.reload_settings) ++
      (ifD cfg.wake-lock.enable termux-tools.wake_lock) ++
      (ifD cfg.wake-unlock.enable termux-tools.wake_unlock) ++
      (ifD cfg.xdg-open.enable termux-tools.xdg_open);

    environment.loginHook = lib.mkIf cfg.wakeLock.enable ''
      ${termux-tools.wake_lock}/bin/droid-nix-wake-lock || true
    '';
  };
}
