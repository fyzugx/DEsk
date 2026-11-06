# DEsk on NixOS

This flake builds DEsk from the same repository checkout, with a private
wlroots 0.18.3. The build adjusts paths and session integration for NixOS;
the module can generate machine-wide defaults for DEsk's existing settings.

It was contributed by picarica (PR #48) and is maintained by the community:
DEsk's own releases are not built or tested with Nix. If it stops building,
please open an issue or a pull request. `version` in `package.nix` has to
match the one in DEsk's `meson.build`.

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

For a local checkout, use
`git+file:///absolute/path/to/DEsk?dir=packaging/nix` instead. Rebuild, log out and choose **DEsk** in your
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

To declare machine defaults with the NixOS module:

```nix
programs.desk.settings = {
  app.terminal = "org.kde.konsole.desktop";
  natural_scrolling = true;
};
```

This writes `/etc/xdg/desk/shell.conf`. Nested attributes become dotted keys,
and booleans become `yes` or `no`; values can also be single-line strings or
numbers. An empty settings set leaves the file unmanaged. Personal settings
take precedence, and applications named by desktop ID must be installed
separately. Settings in the Nix store are public, so do not put secrets in them.

## Validation

The flake includes package checks, an isolated headless compositor/login-wrapper
smoke test and NixOS module evaluation. x86_64-linux checks have passed; the
flake also exposes aarch64-linux outputs, but ARM and interactive
login/unlock/hardware behaviour have not been validated.