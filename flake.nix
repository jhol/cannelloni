{
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-22.11";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, utils }:
    let
      linuxSystems = [
        "aarch64-linux"
        "x86_64-linux"
      ];
    in
    utils.lib.eachSystem linuxSystems (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in
      rec {
        packages = rec {
          default = pkgs.stdenv.mkDerivation rec {
            name = "cannelloni";
            src = ./.;

            nativeBuildInputs = with pkgs; [
              autoconf
              automake
              pkg-config
              sdcc
            ];

            buildInputs = with pkgs; [
              libusb1
            ];

            strictDeps = true;

            preConfigure = ''
              ./autogen.sh
            '';

            postInstall = ''
              mkdir -p $out/etc/udev
              cp -r udev_rule/ $out/etc/udev/rules.d
            '';
          };
        };

        nixosModules = { config, pkgs, ... }: {
          config = {
            services.udev.packages = [ packages.default ];
          };
        };
      }
    );
}
