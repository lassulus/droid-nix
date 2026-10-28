# Android APIs

The app has [Termux:API](https://wiki.termux.com/wiki/Termux:API) built in;
there is no separate app to install. To get the commands of
[termux-api-package](https://github.com/termux/termux-api-package), named
`droid-nix-*` instead of `termux-*`, add to
`~/.config/droid-nix/droid-nix.nix`:

```nix
  android-integration.api.enable = true;
```

and run `droid-nix switch`.

Examples:

```sh
droid-nix-notification --id demo -t droid-nix -c "Tap the button" \
  --button1 "Write file" --button1-action "date >> ~/button.log"
droid-nix-battery-status
echo hello | droid-nix-clipboard-set
droid-nix-clipboard-get
droid-nix-location
```

Commands that need a runtime permission (location, camera, microphone,
contacts, SMS, ...) make Android ask for it on the first call and print an
error; allow it and run the command again. `droid-nix-location` also needs
location turned on, and prints nothing if it gets no fix;
`droid-nix-location -p network -r last` returns the last known location.

Other apps (Tasker, ...) can run commands in droid-nix through the
RUN_COMMAND intent, see
[RUN_COMMAND in the README](../README.md#run_command-tasker-and-other-apps).
