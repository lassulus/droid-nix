{ config, lib, pkgs, ... }:

let
  cfg = config.services.sshd;

  # Host key, pid file and log; not in the store.
  stateDir = "${config.user.home}/.local/state/sshd";
  hostKey = "${stateDir}/ssh_host_ed25519_key";

  sshdConfig = pkgs.writeText "sshd_config" ''
    Port ${toString cfg.port}
    HostKey ${hostKey}
    PidFile ${stateDir}/sshd.pid
    AuthorizedKeysFile /etc/ssh/authorized_keys
    PasswordAuthentication no
    KbdInteractiveAuthentication no
    Subsystem sftp ${pkgs.openssh}/libexec/sftp-server
  '';
in
{

  ###### interface

  options.services.sshd = {

    enable = lib.mkEnableOption ''
      an SSH server. Android has no service manager: it is started at every
      login and switch unless it already runs, and logs to
      ~/.local/state/sshd/sshd.log'';

    port = lib.mkOption {
      type = lib.types.port;
      default = 8022;
      description = "Port to listen on. Apps cannot use ports below 1024.";
    };

    authorizedKeys = lib.mkOption {
      type = lib.types.listOf lib.types.str;
      default = [ ];
      example = [ "ssh-ed25519 AAAA... you@pc" ];
      description = "Public keys that may log in.";
    };

  };

  ###### implementation

  config = lib.mkIf cfg.enable {

    environment.etc."ssh/authorized_keys".text = lib.concatLines cfg.authorizedKeys;

    # `ssh phone command` runs a bash that is not a login shell; this gives
    # it the environment of a login.
    environment.etc."bashrc".text = lib.mkDefault ''
      . /etc/profile
    '';

    android-integration.wakeLock.enable = lib.mkDefault true;

    environment.loginHook = ''
      if ! kill -0 "$(${pkgs.coreutils}/bin/cat ${stateDir}/sshd.pid 2>/dev/null)" 2>/dev/null; then
        ${pkgs.coreutils}/bin/mkdir --parents ${stateDir}
        [ -e ${hostKey} ] || ${pkgs.openssh}/bin/ssh-keygen -q -t ed25519 -N "" -f ${hostKey} || true
        ${pkgs.openssh}/bin/sshd -f ${sshdConfig} -E ${stateDir}/sshd.log || true
      fi
    '';

  };

}
