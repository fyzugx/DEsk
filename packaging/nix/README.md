# DEsk on NixOS

This flake builds DEsk from the same repository checkout, with a private
wlroots 0.18.3. The build adjusts paths and session integration for NixOS;
it does not add DEsk settings or application-selection features.

## Build and try

From the repository root:

```sh
nix build 'path:.?dir=packaging/nix'
DESK_SIZE=1280x800 nix run 'path:.?dir=packaging/nix'
nix flake check 'path:.?dir=packaging/nix' -L
```

The default app runs DEsk in a window inside an existing desktop. It does not
install the system services needed for a full login session. The commands use
`path:` so new, untracked packaging files are included during development.

## NixOS login session

Add the input and module to your existing system flake, keeping your own host
name and nixpkgs configuration:

```nix
{
  inputs.desk.url = "github:fyzugx/DEsk?dir=packaging/nix";

  outputs = { nixpkgs, desk, ... }: {
    nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ./configuration.nix
        desk.nixosModules.default
        { programs.desk.enable = true; }
      ];
    };
  };
}
```

The upstream URL will work after this directory is merged; before then, use
`git+file:///absolute/path/to/DEsk?dir=packaging/nix` for committed local work,
or your DEsk fork and branch. Rebuild, log out and choose **DEsk** in your
existing login manager. The module does not configure a login manager or
networking; Bluetooth and printer integration follow the host's settings.

The module supplies PAM, polkit, keyring, audio defaults, portals, Xwayland,
fonts and supporting desktop services. Recommended applications, control tools
and session helpers are installed by default. Setting
`programs.desk.recommendedPackages = false` excludes that entire optional bundle,
including `wdisplays`, `nwg-look`, NetworkManager applet/editor, `udiskie` and
`poweralertd`, as well as the terminal, browser, file manager and other apps.
Core services remain enabled, but excluded tools must be installed separately
if wanted; having another desktop installed does not replace every helper.

## DEsk settings

Use Shell Settings or the existing `desk/shell.conf` configuration; see
[the upstream configuration reference](../../docs/CONFIGURATION.md). For example,
the personal file can select installed applications by desktop ID:

```ini
app.terminal = org.kde.konsole.desktop
app.files = org.kde.dolphin.desktop
app.editor = org.kde.kate.desktop
```

There is no `programs.desk.settings` option. Machine defaults can still be
declared with NixOS's existing file-management option, without modifying DEsk:

```nix
environment.etc."xdg/desk/shell.conf".text = ''
  app.terminal = org.kde.konsole.desktop
  natural_scrolling = yes
'';
```

Personal settings take precedence; this does not overwrite the user's file.
Settings in the Nix store are public, so do not put secrets in them.

## Validation

The flake includes package checks, an isolated headless compositor/login-wrapper
smoke test and NixOS module evaluation. x86_64-linux checks have passed; the
flake also exposes aarch64-linux outputs, but ARM and interactive
login/unlock/hardware behaviour have not been validated.