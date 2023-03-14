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
        appName = "cannelloni";
      in
      rec {
        packages = rec {
          firmware = pkgs.stdenv.mkDerivation (
            let
              hexFile = "fx2pipe.ihx";
            in
            rec {
              name = "${appName}-firmware";
              src = ./firmware;

              nativeBuildInputs = with pkgs; [
                sdcc
              ];

              buildPhase = ''
                make CC="sdcc -mmcs51" ${hexFile}
              '';

              installPhase = ''
                mkdir -p $out/share/${appName}
                cp ${hexFile} $out/share/${appName}
              '';
            }
          );

          default = pkgs.stdenv.mkDerivation rec {
            name = appName;
            src = ./.;

            buildInputs = with pkgs; [
              libusb1
            ];

            installPhase = ''
              mkdir -p $out/bin
              cp ${appName} $out/bin

              mkdir -p $out/etc/udev
              cp -r udev_rule/ $out/etc/udev/rules.d

              cp -r ${firmware}/share $out
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
