{
  description = "Synchronized distributed audio recording";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }: let 
    systems = [ "x86_64-linux" "aarch64-linux" ];
  in flake-utils.lib.eachSystem systems (system: let
    pkgs = nixpkgs.legacyPackages.${system};
  in {
    packages.default = pkgs.callPackage ./default.nix {
      nixpkgs = import nixpkgs;
    };
    devShells = {
      nrf53 = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        platform = "nrf53";
        dev = true;
      };
      simulator = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        platform = "simulator";
        dev = true;
      };
      model = pkgs.callPackage ./model/shell.nix { };
    };
  });
}
