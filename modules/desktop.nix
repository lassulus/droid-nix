{ config, lib, pkgs, androidApp, ... }:

let
  cfg = config.desktop;
  # The app's "droid-nix desktop" activity (app/.../DesktopActivity.java)
  # gets its frames from droid-nix-desktop-server on this socket and sends
  # touch and keys back.
  socket = "${androidApp.dataDir}/files/desktop.sock";
  server = pkgs.callPackage ../pkgs/droid-nix-desktop-server { };
  # fontconfig's defaults (aliases like monospace) and a font for them;
  # there is no /etc/fonts here.
  fontsConf = pkgs.writeText "fonts.conf" ''
    <?xml version="1.0"?>
    <!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
    <fontconfig>
      <include>${pkgs.fontconfig.out}/etc/fonts/fonts.conf</include>
      <include>${pkgs.fontconfig.out}/etc/fonts/conf.d</include>
      <dir>${pkgs.dejavu_fonts}/share/fonts</dir>
    </fontconfig>
  '';
  dbus = pkgs.dbus.out;
  # Default session: headless sway (your ~/.config/sway/config, else sway's
  # default one) with droid-nix-desktop-server.
  sway = pkgs.writeShellScript "droid-nix-desktop-sway" ''
    config=''${XDG_CONFIG_HOME:-$HOME/.config}/sway/config
    [ -e "$config" ] || config=${pkgs.sway}/etc/sway/config
    export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
    # sway starts swaybar and swaynag from PATH.
    export PATH=${pkgs.sway}/bin:$PATH
    {
      printf 'include %s\n' "$config"
      printf 'exec ${server}/bin/droid-nix-desktop-server\n'
      [ $# = 0 ] || printf 'exec %s\n' "$(printf '%q ' "$@")"
    } >"$XDG_RUNTIME_DIR/droid-nix-desktop.conf"
    exec ${pkgs.sway}/bin/sway -c "$XDG_RUNTIME_DIR/droid-nix-desktop.conf"
  '';
  droid-nix-desktop = pkgs.writeShellScriptBin "droid-nix-desktop" ''
    export XDG_RUNTIME_DIR=''${XDG_RUNTIME_DIR:-$TMPDIR}
    export FONTCONFIG_FILE=''${FONTCONFIG_FILE:-${fontsConf}}
    export DROID_NIX_DESKTOP_SOCKET=${socket}
    ${pkgs.coreutils}/bin/rm -f ${socket}
    # No /etc/dbus-1 here: give the session bus its config.
    exec ${dbus}/bin/dbus-run-session --dbus-daemon=${dbus}/bin/dbus-daemon \
      --config-file=${dbus}/share/dbus-1/session.conf -- \
      ${cfg.session} "$@"
  '';
in
{

  ###### interface

  options.desktop = {
    enable = lib.mkEnableOption ''
      a `droid-nix-desktop [command...]` command that starts
      {option}`desktop.session` for the app's "droid-nix desktop" icon,
      which shows it fullscreen and passes touch and keys to it. The
      command runs in the foreground; arguments are passed to the session
      (the default one starts them in sway, e.g. `droid-nix-desktop foot`)'';
    session = lib.mkOption {
      type = lib.types.str;
      default = "${sway}";
      description = ''
        Command that starts the desktop, with the arguments of
        `droid-nix-desktop`. It must run `droid-nix-desktop-server` in it,
        which serves the app on the unix socket in
        `$DROID_NIX_DESKTOP_SOCKET`. That works in Wayland compositors with
        wlr-screencopy-unstable-v1, wlr-virtual-pointer-unstable-v1 and
        virtual-keyboard-unstable-v1 (and wlr-output-management-unstable-v1
        to fit the output to the screen), e.g. wlroots ones; the app shows
        their first output. Default: headless sway with it.
      '';
    };
  };

  ###### implementation

  config = lib.mkIf cfg.enable {
    environment.packages = [ droid-nix-desktop server ];
  };
}
