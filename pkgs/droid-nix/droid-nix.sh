#!@bash@/bin/bash

# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.

PATH=@coreutils@/bin:@nix@/bin:${PATH:+:}$PATH

set -eu
set -o pipefail

PROFILE_DIRECTORY="/nix/var/nix/profiles/droid-nix"

function errorEcho() {
    >&2 echo "$@"
}

function setupPasstroughOpts() {
    if [[ -v VERBOSE ]]; then
        PASSTHROUGH_OPTS+=(--show-trace)
    fi
}

function nixActivationPackage() {
    local command="$1"
    local extraArgs=("${@:2}"
                     --extra-experimental-features "flakes nix-command"
                     "${PASSTHROUGH_OPTS[@]}"
                     --impure "${FLAKE_CONFIG_URI}.activationPackage")

    nix "${command}" "${extraArgs[@]}"
}


function doBuild() {
    echo "Building activation package..."
    nixActivationPackage build
}

function doGenerations() {
    nix-env --profile $PROFILE_DIRECTORY --list-generations
}

function doHelp() {
    echo "Usage: $0 [OPTION] COMMAND"
    echo
    echo "Options"
    echo
    echo "  -h|--help         Print this help"
    echo "  -n|--dry-run      Do a dry run, only prints what actions would be taken"
    echo "  -v|--verbose      Verbose output"
    echo "  -F|--flake FLAKE  Path to flake and device name (e.g. path/to/flake#device),"
    echo "                    device 'default' will be used if no attribute name is given,"
    echo "                    defaults to ~/.config/droid-nix"
    echo
    echo "Options passed on to nix build"
    echo
    echo "  --builders BUILDERS"
    echo "  --cores NUM"
    echo "  --keep-failed"
    echo "  --keep-going"
    echo "  --max-jobs NUM"
    echo "  --option NAME VALUE"
    echo "  --override-input INPUT URL"
    echo "  --show-trace"
    echo
    echo "Commands"
    echo
    echo "  generations     Show all generations"
    echo
    echo "  help            Print this help"
    echo
    echo "  rollback        Rollback and activate configuration"
    echo
    echo "  build           Build configuration"
    echo
    echo "  switch          Build and activate configuration"
    echo
    echo "  switch-generation NUM"
    echo "                  Switch generation and activate configuration"
}

function doSwitch() {
    echo "Building activation package..."
    nixActivationPackage build --no-link

    echo "Executing activation script..."
    generationDir="$(nixActivationPackage path-info)"

    "${generationDir}/activate"
}

function doSwitchGeneration() {
    local generationNum=$1

    if [[ -x "${PROFILE_DIRECTORY}-${generationNum}-link/activate" ]]; then
        echo "Executing activation script..."
        "${PROFILE_DIRECTORY}-${generationNum}-link/activate"
    else
        errorEcho "Activation was not successful, generation is either broken or already garbage collected."
        errorEcho "See 'droid-nix generations' for available generations."
        exit 1
    fi
}


COMMAND_ARGS=()
COMMAND=
FLAKE="$HOME/.config/droid-nix"
PASSTHROUGH_OPTS=()

while [[ $# -gt 0 ]]; do
    opt="$1"
    shift
    case $opt in
        build|generations|help|rollback|switch|switch-generation)
            COMMAND="$opt"
            ;;
        -F|--flake)
            FLAKE="$1"
            shift
            ;;
        -h|--help)
            doHelp
            exit 0
            ;;
        -n|--dry-run)
            export DRY_RUN=1
            ;;
        --option|--override-input)
            PASSTHROUGH_OPTS+=("$opt" "$1" "$2")
            shift 2
            ;;
        --builders|--cores|--max-jobs)
            PASSTHROUGH_OPTS+=("$opt" "$1")
            shift
            ;;
        --keep-failed|--keep-going|--show-trace)
            PASSTHROUGH_OPTS+=("$opt")
            ;;
        -v|--verbose)
            export VERBOSE=1
            ;;
            *)
                case $COMMAND in
                    switch-generation)
                        COMMAND_ARGS+=("$opt")
                        ;;
                    *)
                        errorEcho "$0: unknown option '$opt'"
                        errorEcho "Run '$0 --help' for usage help"
                        exit 1
                        ;;
                esac
            ;;
    esac
done

# add "droidNixConfigurations." as prefix in attribute name, e.g.
# /path/to/flake#device -> /path/to/flake#droidNixConfigurations.device
# if no attribute name given, use "default"
if [[ "$FLAKE" =~ \# ]]; then
    FLAKE_CONFIG_URI="${FLAKE%#*}#droidNixConfigurations.${FLAKE#*#}"
else
    FLAKE_CONFIG_URI="${FLAKE}#droidNixConfigurations.default"
fi

setupPasstroughOpts

if [[ -z $COMMAND ]]; then
    doHelp >&2
    exit 1
fi

case $COMMAND in
    build)
        doBuild
        ;;
    generations)
        doGenerations
        ;;
    help)
        doHelp
        ;;
    rollback)
        if [[ $(readlink $PROFILE_DIRECTORY) =~ ^droid-nix-([0-9]+)-link$ ]]; then
            doSwitchGeneration $((BASH_REMATCH[1] - 1))
        else
            errorEcho "droid-nix profile link is broken, please run 'droid-nix switch' to fix it."
            exit 1
        fi
        ;;
    switch)
        doSwitch
        ;;
    switch-generation)
        if [[ ${#COMMAND_ARGS[@]} -eq 1 ]]; then
            doSwitchGeneration "${COMMAND_ARGS[0]}"
        else
            errorEcho "switch-generation expects one argument, got ${#COMMAND_ARGS[@]}."
            exit 1
        fi
        ;;
    *)
        errorEcho "Unknown command: $COMMAND"
        doHelp >&2
        exit 1
        ;;
esac
