# Running an SSH server

Android has no service manager, so the SSH server is a background process
that you start yourself. It runs as your droid-nix user and can only log in
as that user (`droid-nix`). It listens on port 8022, since apps cannot bind
ports below 1024.

## Configuration

`~/.config/droid-nix/droid-nix.nix`:

```nix
{ config, lib, pkgs, ... }:

{
  environment.packages = with pkgs; [
    vim
    openssh
    procps
    nettools # ifconfig
  ];

  environment.etc."ssh/sshd_config".text = ''
    Port 8022
    HostKey ${config.user.home}/.ssh/ssh_host_ed25519_key
    PidFile ${config.user.home}/.ssh/sshd.pid
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

This took 23 seconds on a Pixel 8a; the wake lock commands are built on the
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

sshd must be started with its absolute path:

```sh
~/.nix-profile/bin/sshd -E ~/.ssh/sshd.log
```

It logs to `~/.ssh/sshd.log`. To stop it: `kill $(cat ~/.ssh/sshd.pid)`.
To start it with every new session, add that line to `~/.bash_profile`; if
it already runs, the new one logs "Address already in use" and exits.

## Keep it running with the screen off

Hold a wake lock while the server should be reachable: run
`termux-wake-lock`, or expand the droid-nix notification and tap
**Acquire wakelock**. Android then asks "Let app always run in background?":
allow it. `termux-wake-unlock` or **Release wakelock** releases it.

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
