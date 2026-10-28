#!@bash@/bin/bash

# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

set -eu

usage() {
    echo "usage: droid-nix switch|build [--flake REF[#NAME]] [nix options...]" >&2
    exit 1
}

[[ $# -gt 0 ]] || usage
command=$1
shift
[[ $command = switch || $command = build ]] || usage

flake=$HOME/.config/droid-nix
nixArgs=()
while [[ $# -gt 0 ]]; do
    if [[ $1 = --flake ]]; then
        [[ $# -gt 1 ]] || usage
        flake=$2
        shift 2
    else
        nixArgs+=("$1")
        shift
    fi
done
[[ $flake = *#* ]] || flake+="#default"

build=(@nix@/bin/nix --extra-experimental-features "nix-command flakes" build
       "${nixArgs[@]}" --impure "${flake%%#*}#droidNixConfigurations.${flake#*#}.activationPackage")

if [[ $command = build ]]; then
    exec "${build[@]}"
fi
generation=$("${build[@]}" --no-link --print-out-paths)
exec "$generation/activate"
