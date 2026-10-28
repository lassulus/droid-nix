# Running an SSH server

Android has no service manager, so the SSH server is a background process
that you start yourself. It runs as your droid-nix user and can only log in
as that user (`droid-nix`). It listens on port 8022, since apps cannot bind
ports below 1024.

sshd's own listener does not work in droid-nix yet: Android blocks the
`accept` system call for apps (only `accept4` is allowed), and a plain
`sshd` logs "accept: Function not implemented" in a loop. So
`systemd-socket-activate` listens on the port instead and starts `sshd -i`
for each connection.

## Configuration

`~/.config/droid-nix/droid-nix.nix`:

```nix
{ config, lib, pkgs, ... }:

let
  # Listens on port 8022 in the background and runs `sshd -i` for each
  # connection. Does nothing if it already runs.
  sshd-start = pkgs.writeShellScriptBin "sshd-start" ''
    if ${pkgs.procps}/bin/pgrep -f systemd-socket-activate >/dev/null; then
      exit 0
    fi
    nohup ${pkgs.systemdMinimal}/bin/systemd-socket-activate -l 8022 --inetd -a \
      ${pkgs.openssh}/bin/sshd -i >>"$HOME/.ssh/sshd.log" 2>&1 &
  '';
in
{
  environment.packages = with pkgs; [
    vim
    openssh
    procps
    nettools # ifconfig
    sshd-start
  ];

  environment.etc."ssh/sshd_config".text = ''
    HostKey ${config.user.home}/.ssh/ssh_host_ed25519_key
    PasswordAuthentication no
    KbdInteractiveAuthentication no
  '';

  # `ssh phone command` runs a bash that is not a login shell; this gives
  # it the same environment (PATH, ...) as a terminal session.
  environment.etc."bashrc".text = ''
    . /etc/profile
  '';

  android-integration.termux-wake-lock.enable = true;
  android-integration.termux-wake-unlock.enable = true;

  # Backup etc files instead of failing to activate generation if a file already exists in /etc
  environment.etcBackupExtension = ".bak";

  # Set up nix for flakes
  nix.extraOptions = ''
    experimental-features = nix-command flakes
  '';
}
```

```sh
droid-nix switch
```

This took 73 seconds on a Pixel 8a; the wake lock commands are built on the
phone.

## Keys

Create the host key and add your public key (the contents of
`~/.ssh/id_ed25519.pub` on your PC; paste it into the terminal with a long
press, Paste):

```sh
mkdir -p ~/.ssh && chmod 700 ~/.ssh
ssh-keygen -t ed25519 -N "" -f ~/.ssh/ssh_host_ed25519_key
echo 'ssh-ed25519 AAAA... you@pc' >> ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys
```

## Start it

```sh
sshd-start
```

Connections are logged to `~/.ssh/sshd.log`. To stop the server:
`pkill -f systemd-socket-activate`.

To start it with every new session, add it to `~/.bash_profile`:

```sh
echo sshd-start >> ~/.bash_profile
```

`~/.bash_profile` is only read once droid-nix has been closed and opened
again after the first start (see [getting-started.md](getting-started.md)).
A running server also keeps droid-nix from installing a changed login
script (for example after changing `user.shell`): stop it and exit all
sessions first.

## Keep it running with the screen off

Hold a wake lock while the server should be reachable. Either expand the
droid-nix notification and tap **Acquire wakelock**, or run
`termux-wake-lock`. `termux-wake-lock` only works if droid-nix may "Display
over other apps" (Settings, Apps, droid-nix); otherwise it prints an error
that says so. Android then asks "Let app always run in background?": allow
it. **Release wakelock** or `termux-wake-unlock` releases it.

With the wake lock held, the server answered over Wi-Fi after two minutes
with the screen off, and in forced deep Doze.

## Connect

Find the phone's address with `ifconfig` (the `inet` address of `wlan0`), or
in Android's Settings, About phone, IP address. `ip addr` does not work:
Android does not let apps use netlink.

```sh
ifconfig
```

From your PC:

```sh
ssh -p 8022 droid-nix@192.168.1.23
```

Over USB with adb, without Wi-Fi:

```sh
adb forward tcp:8022 tcp:8022
ssh -p 8022 droid-nix@localhost
```

Both interactive logins and commands (`ssh -p 8022 droid-nix@localhost
uname -a`) work.
