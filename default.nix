{ nixpkgs, lib, stdenv
, dev ? false
}: let
  optimizeSizeOverlay = final: prev: lib.optionalAttrs prev.stdenv.hostPlatform.isNone {
    # Zephyr doesn't use newlib or libstdc++ by default, but this doesn't hurt
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
        arch = "armv8-m.main+fp";
        tune = "cortex-m33";
        float = "hard";
      };
    };
    overlays = [ optimizeSizeOverlay ];
  };
in pkgs.callPackage ({
  lib, stdenv, callPackage, buildPackages, cmake, ninja, python3, dtc
, python3Packages, git, nix-prefetch-git, clang-tools
}: stdenv.mkDerivation {
  pname = "zeus-le";
  version = "0.1.0";

  src = if dev
    then null
    else callPackage ./firmware/west.nix { };

  nativeBuildInputs = [
    cmake
    ninja
    python3
    dtc
  ] ++ (with python3Packages; [
    pyelftools
    pykwalify
    packaging
  ]) ++ (lib.optionals dev [
    git
    python3Packages.west
    nix-prefetch-git
    (buildPackages.clang-tools.override {
      llvmPackages = buildPackages.llvmPackages_latest;
    })
  ]);

  env = {
    ZEPHYR_TOOLCHAIN_VARIANT = "cross-compile";
    CROSS_COMPILE = "${stdenv.cc}/bin/${stdenv.cc.targetPrefix}";
  } // lib.optionalAttrs dev {
    # Required by menuconfig
    LOCALE_ARCHIVE = "${buildPackages.glibcLocales}/lib/locale/locale-archive";
  };

  cmakeDir = "../zephyr/share/sysbuild";

  cmakeFlags = [
    "-DBOARD=raytac_mdbt53_db_40_nrf5340_cpuapp"
    "-DAPP_DIR=../firmware/central"
    # TODO: generate dynamically
    "-DBUILD_VERSION=zephyr-v3.5.0"
  ];

  preConfigure = ''
    export XDG_CACHE_HOME="$TMPDIR"
    # Zephyr requires absolute paths
    export CC="$(command -v $CC)"
    export CXX="$(command -v $CXX)"

    source .zephyr-env
  '';
  
  installPhase = ''
    runHook preInstall

    mkdir -p "$out"
    cp central/zephyr/zephyr.elf "$out/zeus_le_central_app.elf"
    cp central_net/zephyr/zephyr.elf "$out/zeus_le_central_net.elf"

    runHook postInstall
  '';
  
  dontFixup = true;
}) { }
