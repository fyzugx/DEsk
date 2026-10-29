{
  description = "DEsk Wayland desktop: package and NixOS session module";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      packagesFor =
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          desk = pkgs.callPackage ./package.nix {
            wlroots = pkgs.callPackage ./wlroots-0.18.nix { };
          };
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          packages = packagesFor system;
        in
        packages // { default = packages.desk; }
      );

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.desk}/bin/desk-window";
          meta.description = "Try DEsk inside the current desktop";
        };
        session = {
          type = "app";
          program = "${self.packages.${system}.desk}/bin/desk-session";
          meta.description = "Start a standalone DEsk session from a TTY";
        };
      });

      overlays.default = final: prev: {
        desk = final.callPackage ./package.nix {
          wlroots = final.callPackage ./wlroots-0.18.nix { };
        };
      };

      nixosModules.default = import ./module.nix self;

      checks = forAllSystems (system: {
        package = self.packages.${system}.desk;
        headless = import ./tests/headless.nix {
          pkgs = import nixpkgs { inherit system; };
          desk = self.packages.${system}.desk;
        };
        module = import ./tests/module.nix { inherit self nixpkgs system; };
      });

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt);
    };
}
