self:
{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.programs.desk;
  settingType = lib.types.attrsOf settingValueType;
  settingValueType =
    lib.types.oneOf [
      lib.types.bool
      lib.types.int
      lib.types.float
      (lib.types.strMatching "[^\n\r]*")
      settingType
    ]
    // {
      description = "single-line string, boolean, number or attribute set of DEsk settings";
    };
  flattenSettings =
    prefix: attrs:
    lib.concatLists (
      lib.mapAttrsToList (
        name: value:
        let
          key = if prefix == "" then name else "${prefix}.${name}";
        in
        if builtins.isAttrs value then
          flattenSettings key value
        else
          [
            {
              name = key;
              inherit value;
            }
          ]
      ) attrs
    );
  settingsEntries = flattenSettings "" cfg.settings;
  flatSettings = builtins.listToAttrs settingsEntries;
  settingLine =
    key: value:
    "${key} = ${if builtins.isBool value then (if value then "yes" else "no") else toString value}";
  portalConfig =
    (pkgs.formats.ini { }).generate "other-wlr-portals.conf"
      config.xdg.portal.wlr.settings;
  # NixOS passes --config, which otherwise bypasses upstream's per-desktop
  # discovery. Keep other desktops' configured chooser when switching back.
  portalLauncher = pkgs.writeShellScript "desk-portal-wlr" ''
    case ":''${XDG_CURRENT_DESKTOP:-}:" in
      *:DEsk:*) exec ${pkgs.xdg-desktop-portal-wlr}/libexec/xdg-desktop-portal-wlr \
        --config=${cfg.package}/share/desk/xdg-desktop-portal-wlr/DEsk ;;
      *) exec ${pkgs.xdg-desktop-portal-wlr}/libexec/xdg-desktop-portal-wlr \
        --config=${portalConfig} ;;
    esac
  '';
in
{
  options.programs.desk = {
    enable = lib.mkEnableOption "the DEsk Wayland desktop session";
    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.desk;
      defaultText = lib.literalExpression "inputs.desk.packages.\${pkgs.stdenv.hostPlatform.system}.desk";
      description = "The DEsk package to install.";
    };
    recommendedPackages = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Install the upstream-style desktop applications, control tools and session helpers. Does not replace the host's networking configuration.";
    };
    settings = lib.mkOption {
      type = settingType;
      default = { };
      example = lib.literalExpression ''
        {
          graphics = "high";
          natural_scrolling = true;
          scale = 1.25;
          app.terminal = "org.kde.konsole.desktop";
          app.files = "org.kde.dolphin.desktop";
        }
      '';
      description = ''
        Machine defaults written to /etc/xdg/desk/shell.conf. Nested attributes
        become dotted keys; values may be single-line strings, booleans or
        numbers. Personal desk/shell.conf settings take precedence. An empty
        attribute set leaves the file unmanaged. This file is public: do not
        put passwords or other secrets here.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    assertions = [
      {
        assertion = lib.all (
          key:
          builtins.match "[A-Za-z0-9_.-]+" key != null
          && builtins.stringLength (settingLine key flatSettings.${key}) < 500
        ) (builtins.attrNames flatSettings);
        message = "programs.desk.settings keys must use letters, digits, _, . or - and each line must be shorter than 500 characters.";
      }
      {
        assertion = builtins.length settingsEntries == builtins.length (builtins.attrNames flatSettings);
        message = "programs.desk.settings must not define the same dotted key in both nested and flat form.";
      }
    ];
    environment.etc."xdg/desk/shell.conf" = lib.mkIf (flatSettings != { }) {
      text = lib.generators.toKeyValue { mkKeyValue = settingLine; } flatSettings;
    };
    environment.systemPackages = [
      cfg.package
    ]
    ++ lib.optionals cfg.recommendedPackages [
      pkgs.xfce4-terminal
      pkgs.mousepad
      pkgs.ristretto
      pkgs.atril
      pkgs.engrampa
      pkgs.parole
      pkgs.firefox
      pkgs.zip
      pkgs.unzip
      pkgs.pavucontrol
      pkgs.wdisplays
      pkgs.nwg-look
      pkgs.xfce4-settings
      pkgs.xfce4-taskmanager
      pkgs.gnome-disk-utility
      pkgs.baobab
      pkgs.seahorse
      pkgs.hardinfo2
      pkgs.networkmanagerapplet
      pkgs.udiskie
      pkgs.poweralertd
      pkgs.adwaita-icon-theme
    ];
    services.displayManager.sessionPackages = [ cfg.package ];
    services.graphical-desktop.enable = true;

    # Generate a NixOS PAM stack, not upstream's Debian-style "include login".
    security.pam.services.desk = { };
    security.polkit.enable = true;
    programs.dconf.enable = lib.mkDefault true;
    programs.xwayland.enable = lib.mkDefault true;
    services.gnome.gnome-keyring.enable = lib.mkDefault true;
    services.dbus.enable = true;
    services.upower.enable = lib.mkDefault true;
    services.udisks2.enable = lib.mkDefault true;
    services.gvfs.enable = lib.mkDefault true;
    services.tumbler.enable = lib.mkDefault true;
    services.accounts-daemon.enable = lib.mkDefault true;
    services.colord.enable = lib.mkDefault true;
    security.rtkit.enable = lib.mkDefault true;

    # The graphical-desktop module supplies PipeWire/ALSA/Pulse defaults;
    # the host can explicitly keep another audio setup.
    services.pipewire.wireplumber.enable = lib.mkDefault true;
    programs.thunar = lib.mkIf cfg.recommendedPackages {
      enable = true;
      plugins = [ pkgs.thunar-archive-plugin ];
    };
    services.blueman.enable = lib.mkIf config.hardware.bluetooth.enable (lib.mkDefault true);
    programs.system-config-printer.enable = lib.mkIf config.services.printing.enable (
      lib.mkDefault true
    );
    services.system-config-printer.enable = lib.mkIf config.services.printing.enable (
      lib.mkDefault true
    );

    xdg.portal = {
      enable = true;
      wlr.enable = true;
      extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
      config.desk = {
        default = [
          "wlr"
          "gtk"
        ];
        "org.freedesktop.impl.portal.Screenshot" = [ "wlr" ];
        "org.freedesktop.impl.portal.ScreenCast" = [ "wlr" ];
        "org.freedesktop.impl.portal.Secret" = [ "gnome-keyring" ];
        "org.freedesktop.impl.portal.Inhibit" = [ "none" ];
      };
    };
    systemd.user.services.xdg-desktop-portal-wlr.serviceConfig.ExecStart = lib.mkForce [
      ""
      "${portalLauncher}"
    ];
    environment.etc."xdg/xdg-desktop-portal-wlr/DEsk".source =
      "${cfg.package}/share/desk/xdg-desktop-portal-wlr/DEsk";

    fonts.packages = [
      pkgs.tex-gyre.pagella
      pkgs.tex-gyre.heros
      pkgs.tex-gyre.cursor
      pkgs.tex-gyre.schola
      pkgs.noto-fonts
      pkgs.noto-fonts-cjk-sans
      pkgs.noto-fonts-color-emoji
    ];
    hardware.graphics.enable = lib.mkDefault true;
  };
}
