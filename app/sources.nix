# The upstream sources of the app, pinned in ./sources.json: termux-app, of
# which the app uses terminal-emulator and terminal-view, unpatched, and
# Termux:API, built into the app with ./termux-api-patches.
#
# Update one to the head of its default branch, or to REV (needs network, run
# from the flake root):
#   nix run .#update-termux-app [-- REV]
#   nix run .#update-termux-api [-- REV]
{
  lib,
  fetchFromGitHub,
  writeShellApplication,
  git,
  jq,
}:
lib.mapAttrs (
  repo:
  { rev, hash }:
  fetchFromGitHub {
    owner = "termux";
    inherit repo rev hash;
    passthru.updateScript = writeShellApplication {
      name = "update-${repo}";
      runtimeInputs = [
        git
        jq
      ];
      text = ''
        rev=''${1:-$(git ls-remote https://github.com/termux/${repo} HEAD | cut -f1)}
        hash=$(nix flake prefetch --json github:termux/${repo}/"$rev" | jq -r .hash)
        jq --indent 2 --arg rev "$rev" --arg hash "$hash" \
          '.["${repo}"] = { rev: $rev, hash: $hash }' app/sources.json > app/sources.json.new
        mv app/sources.json.new app/sources.json
        echo "${repo}: $rev $hash"
      '';
    };
  }
) (lib.importJSON ./sources.json)
