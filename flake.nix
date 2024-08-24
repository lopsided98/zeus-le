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
    packages = {
      zeus-le-audio = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        board = "zeus_le";
        firmware = "audio";
      };

      zeus-le-central = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        board = "zeus_le";
        firmware = "central";
      };

      # These builds fail due to some obscure BabbleSim issue
      /*zeus-le-simulator-audio = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        board = "nrf5340bsim";
        firmware = "audio";
      };

      zeus-le-simulator-central = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        board = "nrf5340bsim";
        firmware = "central";
      };*/
    };

    devShells = {
      nrf53 = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        board = "zeus_le";
        dev = true;
      };

      simulator = pkgs.callPackage ./default.nix {
        nixpkgs = import nixpkgs;
        board = "nrf5340bsim";
        dev = true;
      };

      model = pkgs.callPackage ./model/shell.nix { };
    };
  });
}
