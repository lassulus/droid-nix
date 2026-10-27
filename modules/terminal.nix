# Copyright (c) 2019-2024, see AUTHORS. Licensed under MIT License, see LICENSE.
{ config
, lib
, pkgs
, ...
}:
with lib; let
  cfg = config.terminal;
  validColornames =
    [ "background" "foreground" "cursor" ] ++
    (builtins.map (n: "color${builtins.toString n}") (lib.lists.range 0 15));
  validColorname = colorName: builtins.elem colorName validColornames;
in
{
  ###### interface

  options = {
    terminal = {
      font = mkOption {
        default = null;
        type = types.nullOr types.path;
        example =
          lib.literalExpression
            ''"''${pkgs.terminus_font_ttf}/share/fonts/truetype/TerminusTTF.ttf"'';
        description = ''
          Font used for the terminal.
        '';
      };
      colors = mkOption {
        default = { };
        type = types.lazyAttrsOf types.str;
        example = lib.literalExpression ''
          {
            background = "#000000";
            foreground = "#FFFFFF";
            cursor = "#FFFFFF";
          }
        '';
        description = ''
          Colorscheme used for the terminal.
          Acceptable attribute names are:
          `background`, `foreground`, `cursor` and `color0`-`color15`.
        '';
      };
    };
  };

  ###### implementation

  config = {
    assertions = [{
      assertion = builtins.all validColorname (attrNames cfg.colors);
      message = ''
        `terminal.colors` only accepts the following attributes:
        `background`, `foreground`, `cursor` and `color0`-`color15`.
      '';
    }];

    build.activation =
      let
        fontPath =
          if (lib.strings.hasPrefix "/nix" cfg.font)
          then "${config.build.installationDir}/${cfg.font}"
          else cfg.font;
        configDir = "${config.user.home}/.termux";
        fontTarget = "${configDir}/font.ttf";
        fontBackup = "${configDir}/font.ttf.bak";

        inherit (lib.generators) toKeyValue;

        colors = pkgs.writeTextFile {
          name = "colors.properties";
          text = toKeyValue { } cfg.colors;
        };
        colorsTarget = "${configDir}/colors.properties";
        colorsBackup = "${configDir}/colors.properties.bak";
        colorsPath = "${config.build.installationDir}/${colors}";
      in
      (
        if (cfg.font != null)
        then {
          linkFont = ''
            mkdir -p "${configDir}"
            if [ -e "${fontTarget}" ] && ! [ -L "${fontTarget}" ]; then
              mv "${fontTarget}" "${fontBackup}"
              echo "${fontTarget} has been moved to ${fontBackup}"
            fi
            ln -sf "${fontPath}" "${fontTarget}"
          '';
        }
        else {
          unlinkFont = ''
            if [ -e "${fontTarget}" ] && [ -L "${fontTarget}" ]; then
              rm "${fontTarget}"
              if [ -e "${fontBackup}" ]; then
                mv "${fontBackup}" "${fontTarget}"
                echo "${fontTarget} has been restored from backup"
              else
                if rm -d "${configDir}" 2>/dev/null
                then
                  echo "removed empty ${configDir}"
                fi
              fi
            fi
          '';
        }
      )
      // (
        if (cfg.colors != { })
        then {
          linkColors = ''
            mkdir -p "${configDir}"
            if [ -e "${colorsTarget}" ] && ! [ -L "${colorsTarget}" ]; then
              mv "${colorsTarget}" "${colorsBackup}"
              echo "${colorsTarget} has been moved to ${colorsBackup}"
            fi
            ln -sf "${colorsPath}" "${colorsTarget}"
          '';
        }
        else {
          unlinkColors = ''
            if [ -e "${colorsTarget}" ] && [ -L "${colorsTarget}" ]; then
              rm "${colorsTarget}"
              if [ -e "${colorsBackup}" ]; then
                mv "${colorsBackup}" "${colorsTarget}"
                echo "${colorsTarget} has been restored from backup"
              else
                if rm -d "${configDir}" 2>/dev/null
                then
                  echo "removed empty ${configDir}"
                fi
              fi
            fi
          '';
        }
      );
  };
}
