{ nixpkgs, lib, stdenv

, dev ? false }: let
  optimizeSizeOverlay = final: prev: lib.optionalAttrs prev.stdenv.hostPlatform.isNone {
    stdenv = lib.pipe prev.stdenv [
      # Optimize newlib
      (prev.withCFlags [ "-Os" ])
      # Optimize libstdc++
      (stdenv: prev.overrideCC stdenv (prev.buildPackages.wrapCCWith {
        cc = prev.buildPackages.gcc-unwrapped.overrideAttrs (oldAttrs: {
          EXTRA_FLAGS_FOR_TARGET = oldAttrs.EXTRA_FLAGS_FOR_TARGET ++ [ "-Os" ];
        });
      }))
    ];
  };

  pkgs = nixpkgs {
    localSystem = stdenv.hostPlatform;
    crossSystem = {
      config = "arm-none-eabi";
      libc = "newlib-nano";
      gcc = {
        arch = "armv8-m.main+dsp+fp";
        tune = "cortex-m33";
        float = "hard";
      };
    };
    overlays = [ optimizeSizeOverlay ];
  };
in pkgs.callPackage ({
  lib, stdenv, callPackage, git, cmake, ninja, python3, dtc, python3Packages
, nix-prefetch-git
}: stdenv.mkDerivation {
  pname = "zeus-le";
  version = "0.1.0";

  src = if dev
    then null
    else callPackage ./firmware/west.nix { };

  nativeBuildInputs = [
    git
    cmake
    ninja
    python3
    dtc
  ] ++ (with python3Packages; [
    west
    pyelftools
  ]) ++ (lib.optionals dev [
    nix-prefetch-git
  ]);

  env = {
    GNUARMEMB_TOOLCHAIN_PATH = stdenv.cc;
    ZEPHYR_TOOLCHAIN_VARIANT= "gnuarmemb";
  };

  dontConfigure = true;

  buildPhase = ''
    runHook preBuild

    # Convince Git to read from the Nix store
    export XDG_CONFIG_HOME="$(pwd)/.config"
    mkdir -p "$XDG_CONFIG_HOME/git"
    touch "$XDG_CONFIG_HOME/git/config"
    git config --global --add safe.directory '*'

    west build firmware -- -DUSER_CACHE_DIR="$(pwd)/.cache" -DZEPHYR_GIT_INDEX=/

    runHook postBuild
  '';
  
  installPhase = ''
    runHook preInstall

    mkdir -p "$out"
    cp build/zephyr/zephyr.elf "$out"

    runHook postInstall
  '';
  
  dontFixup = true;
}) { }
