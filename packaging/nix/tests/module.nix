{
  self,
  nixpkgs,
  system,
}:
let
  lib = nixpkgs.lib;
  pkgs = import nixpkgs { inherit system; };
  evaluated = nixpkgs.lib.nixosSystem {
    inherit system;
    modules = [
      self.nixosModules.default
      ({ ... }: {
        programs.desk.enable = true;
        xdg.portal.wlr.settings.screencast.max_fps = 30;
        system.stateVersion = "26.05";
        boot.loader.grub.devices = [ "nodev" ];
        fileSystems."/" = {
          device = "/dev/disk/by-label/test";
          fsType = "ext4";
        };
      })
    ];
  };
  cfg = evaluated.config;
  desk = self.packages.${system}.desk;
  minimal = evaluated.extendModules {
    modules = [
      ({ ... }: {
        programs.desk.recommendedPackages = false;
        services.pulseaudio.enable = true;
        services.pipewire.enable = false;
      })
    ];
  };
  optionalServices = evaluated.extendModules {
    modules = [
      ({ ... }: {
        hardware.bluetooth.enable = true;
        services.printing.enable = true;
      })
    ];
  };
  configured = evaluated.extendModules {
    modules = [
      {
        programs.desk.settings = {
          graphics = "high";
          natural_scrolling = true;
          weather = false;
          scale = 1.25;
          screen_off = 10;
          app = {
            terminal = "org.kde.konsole.desktop";
            files = "org.kde.dolphin.desktop";
          };
          paper."org.gnome.TextEditor" = false;
        };
      }
      { programs.desk.settings.app.editor = "org.kde.kate.desktop"; }
    ];
  };
  emptySettings = evaluated.extendModules {
    modules = [ { programs.desk.settings.app = { }; } ];
  };
  disabled = configured.extendModules {
    modules = [ { programs.desk.enable = lib.mkForce false; } ];
  };
  invalidSettings = [
    { "bad=key" = "value"; }
    { "bad key" = true; }
    { "bad\nkey" = true; }
    { graphics = lib.concatStrings (lib.replicate 500 "x"); }
    {
      app.terminal = "org.kde.konsole.desktop";
      "app.terminal" = "foot.desktop";
    }
  ];
in
assert builtins.all (entry: entry.assertion) cfg.assertions;
assert builtins.isString cfg.system.build.toplevel.drvPath;
assert builtins.elem desk cfg.services.displayManager.sessionPackages;
assert builtins.hasAttr "desk" cfg.security.pam.services;
assert cfg.security.polkit.enable;
assert cfg.programs.xwayland.enable;
assert cfg.xdg.portal.wlr.enable;
assert cfg.services.dbus.enable;
assert cfg.services.pipewire.enable;
assert cfg.services.pipewire.pulse.enable;
assert cfg.services.pipewire.wireplumber.enable;
assert cfg.security.rtkit.enable;
assert cfg.services.gvfs.enable;
assert cfg.services.udisks2.enable;
assert cfg.services.upower.enable;
assert cfg.services.tumbler.enable;
assert cfg.services.gnome.gnome-keyring.enable;
assert cfg.programs.thunar.enable;
assert !cfg.networking.networkmanager.enable;
assert !cfg.hardware.bluetooth.enable;
assert !cfg.services.printing.enable;
assert builtins.all (entry: entry.assertion) minimal.config.assertions;
assert !minimal.config.programs.thunar.enable;
assert !minimal.config.services.pipewire.enable;
assert minimal.config.services.pulseaudio.enable;
assert builtins.all (entry: entry.assertion) configured.config.assertions;
assert builtins.isString configured.config.system.build.toplevel.drvPath;
assert !builtins.hasAttr "xdg/desk/shell.conf" cfg.environment.etc;
assert !builtins.hasAttr "xdg/desk/shell.conf" emptySettings.config.environment.etc;
assert !builtins.hasAttr "xdg/desk/shell.conf" disabled.config.environment.etc;
assert builtins.all (
  settings:
  !builtins.all (entry: entry.assertion)
    (evaluated.extendModules {
      modules = [ { programs.desk.settings = settings; } ];
    }).config.assertions
) invalidSettings;
assert builtins.all
  (
    value:
    !(builtins.tryEval (
      builtins.deepSeq
        (evaluated.extendModules {
          modules = [ { programs.desk.settings.graphics = value; } ];
        }).config.programs.desk.settings
        true
    )).success
  )
  [
    null
    [ "high" ]
    "high\nweather = no"
    "high\n"
    "high\r"
  ];
assert builtins.all (entry: entry.assertion) optionalServices.config.assertions;
assert optionalServices.config.services.blueman.enable;
assert optionalServices.config.programs.system-config-printer.enable;
assert optionalServices.config.services.system-config-printer.enable;
assert cfg.xdg.portal.config.desk."org.freedesktop.impl.portal.ScreenCast" == "wlr";
assert
  cfg.environment.etc."xdg/xdg-desktop-portal-wlr/DEsk".source
  == "${desk}/share/desk/xdg-desktop-portal-wlr/DEsk";
pkgs.runCommand "desk-nixos-module-evaluation" { } ''
  mkdir -p "$out"
  cp ${configured.config.environment.etc."xdg/desk/shell.conf".source} "$out/shell.conf"
  grep -Fx 'app.editor = org.kde.kate.desktop' "$out/shell.conf"
  grep -Fx 'app.files = org.kde.dolphin.desktop' "$out/shell.conf"
  grep -Fx 'app.terminal = org.kde.konsole.desktop' "$out/shell.conf"
  grep -Fx 'graphics = high' "$out/shell.conf"
  grep -Fx 'natural_scrolling = yes' "$out/shell.conf"
  grep -Fx 'paper.org.gnome.TextEditor = no' "$out/shell.conf"
  grep -Fx 'scale = 1.250000' "$out/shell.conf"
  grep -Fx 'screen_off = 10' "$out/shell.conf"
  grep -Fx 'weather = no' "$out/shell.conf"
  cp ${pkgs.writeText "desk-pam" cfg.security.pam.services.desk.text} "$out/pam-desk"
  grep -F pam_unix "$out/pam-desk"
  # Exercise the desktop-specific portal selection without starting a bus
  # service or connecting to the real user's desktop.
  cp ${builtins.elemAt cfg.systemd.user.services.xdg-desktop-portal-wlr.serviceConfig.ExecStart 1} "$out/portal-launcher"
  cat > "$TMPDIR/portal" <<'SH'
  #!${pkgs.runtimeShell}
  printf '%s\n' "$@"
  SH
  chmod +x "$TMPDIR/portal"
  substituteInPlace "$out/portal-launcher" \
    --replace-fail '${pkgs.xdg-desktop-portal-wlr}/libexec/xdg-desktop-portal-wlr' "$TMPDIR/portal"
  for desktop in DEsk 'DEsk:wlroots' 'wlroots:DEsk'; do
    XDG_CURRENT_DESKTOP="$desktop" ${pkgs.runtimeShell} "$out/portal-launcher" \
      | grep -Fx -- '--config=${desk}/share/desk/xdg-desktop-portal-wlr/DEsk'
  done
  for desktop in sway NotDEsk ""; do
    XDG_CURRENT_DESKTOP="$desktop" ${pkgs.runtimeShell} "$out/portal-launcher" > "$TMPDIR/args"
    ! grep -F '${desk}/share/desk/xdg-desktop-portal-wlr/DEsk' "$TMPDIR/args"
    grep -F -- '--config=' "$TMPDIR/args"
  done
''
