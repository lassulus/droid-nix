# Copyright (c) 2019-2022, see AUTHORS. Licensed under MIT License, see LICENSE.

{ bash, nix, runCommandLocal }:

runCommandLocal "droid-nix" { meta.mainProgram = "droid-nix"; } ''
  install -D -m755 ${./droid-nix.sh} $out/bin/droid-nix
  substituteInPlace $out/bin/droid-nix \
    --subst-var-by bash "${bash}" \
    --subst-var-by nix "${nix}"
''
