# Sound

Add to `~/.config/droid-nix/droid-nix.nix`

```nix
  services.pipewire.enable = true;
```

and run `droid-nix switch`. Sound then plays through the droid-nix app, for
PipeWire and PulseAudio programs:

```sh
nix run nixpkgs#mpv -- song.opus
```

PipeWire starts with every new session if it is not running and logs to
`~/.local/state/pipewire/pipewire.log`.
