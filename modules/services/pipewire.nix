{ config, lib, pkgs, androidApp, ... }:

let
  cfg = config.services.pipewire;
  stateDir = "${config.user.home}/.local/state/pipewire";
  # Only sink: the pipe that the app plays, see app/.../AudioPipe.java.
  # PipeWire starts WirePlumber, which links the streams to it.
  pipewireConf = pkgs.writeText "pipewire.conf" ''
    context.properties = { core.daemon = true core.name = pipewire-0 }
    context.spa-libs = {
      audio.convert.* = audioconvert/libspa-audioconvert
      support.* = support/libspa-support
    }
    context.modules = [
      { name = libpipewire-module-protocol-native }
      { name = libpipewire-module-metadata }
      { name = libpipewire-module-spa-node-factory }
      { name = libpipewire-module-client-node }
      { name = libpipewire-module-access }
      { name = libpipewire-module-adapter }
      { name = libpipewire-module-link-factory }
      { name = libpipewire-module-protocol-pulse }
      { name = libpipewire-module-pipe-tunnel
        args = {
          tunnel.mode = sink
          pipe.filename = "${androidApp.dataDir}/files/audio.fifo"
          audio.format = S16LE
          audio.rate = 48000
          audio.channels = 2
          audio.position = [ FL FR ]
          node.name = android
          node.description = "Android"
        }
      }
    ]
    context.objects = [
      { factory = metadata args = { metadata.name = default } }
      { factory = spa-node-factory
        args = {
          factory.name = support.node.driver
          node.name = Dummy-Driver
          priority.driver = 20000
        }
      }
    ]
    context.exec = [ { path = "${cfg.wireplumber.package}/bin/wireplumber" } ]
    pulse.properties = { server.address = [ "unix:${stateDir}/pulse" ] }
  '';
in
{

  ###### interface

  options.services.pipewire = {
    enable = lib.mkEnableOption ''
      sound: a PipeWire server (also for PulseAudio clients) that plays
      through the app. Android has no service manager: it is started at
      every login, switch and boot unless it already runs, and logs to
      ~/.local/state/pipewire/pipewire.log'';
    package = lib.mkPackageOption pkgs "pipewire" { };
    wireplumber.package = lib.mkPackageOption pkgs "wireplumber" { };
  };

  ###### implementation

  config = lib.mkIf cfg.enable {
    environment.packages = [ cfg.package cfg.wireplumber.package ];
    environment.sessionVariables = {
      PIPEWIRE_RUNTIME_DIR = stateDir;
      PULSE_SERVER = "unix:${stateDir}/pulse";
    };
    environment.loginHook = ''
      if ! PIPEWIRE_RUNTIME_DIR=${stateDir} ${cfg.package}/bin/pw-cli info 0 >/dev/null 2>&1; then
        ${pkgs.coreutils}/bin/mkdir --parents ${stateDir}
        PIPEWIRE_RUNTIME_DIR=${stateDir} ${pkgs.util-linux}/bin/setsid --fork \
          ${cfg.package}/bin/pipewire -c ${pipewireConf} >${stateDir}/pipewire.log 2>&1 </dev/null || true
      fi
    '';
  };
}
