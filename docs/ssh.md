# Running an SSH server

Add to `~/.config/droid-nix/droid-nix.nix`, with your public key (the
contents of `~/.ssh/id_ed25519.pub` on your PC; paste with a long press):

```nix
  services.sshd = {
    enable = true;
    authorizedKeys = [ "ssh-ed25519 AAAA... you@pc" ];
  };
```

add `nettools` (for `ifconfig`) to `environment.packages`, and run
`droid-nix switch`. sshd now listens on port 8022 and starts again
with every new session if it is not running; it logs to
`~/.local/state/sshd/sshd.log`. It also starts when the phone boots,
after the first unlock, without opening droid-nix.

It also holds the wake lock so that sshd keeps running with the screen off.
The first time, Android asks "Let app always run in background?": allow it.
**Release wakelock** in the droid-nix notification releases it until the
next session or boot; `android-integration.wakeLock.enable = false` turns it off.

## Connect

The user is `droid-nix`. Over Wi-Fi, use the `inet` address of `wlan0`
from `ifconfig` (without arguments; ignore its `/proc/net/dev` warning):

```sh
ssh -p 8022 droid-nix@192.168.1.23
```

Over USB with adb:

```sh
adb forward tcp:8022 tcp:8022
ssh -p 8022 droid-nix@localhost
```
