{ nixpkgs, pkgs, lib, stdenv
, platform ? "nrf53"
, dev ? false
}: let
  # Optimize newlib for size
  optimizeSizeOverlay = final: prev: lib.optionalAttrs prev.stdenv.hostPlatform.isNone {
    stdenv = prev.withCFlags [ "-Os" ] prev.stdenv;
  };

  # Build an ARM multilib GCC
  multilibOverlay = final: prev: lib.optionalAttrs prev.stdenv.hostPlatform.isNone {
    stdenv = prev.overrideCC prev.stdenv (prev.buildPackages.wrapCCWith {
      cc = (prev.buildPackages.gcc-unwrapped.override {
        enableMultilib = true;
      }).overrideAttrs ({
        configureFlags ? [], ...
      }: {
        configureFlags = configureFlags ++ [ "--with-multilib-list=rmprofile" ];
      });
    });
  };

  pkgs' = {
    "nrf53" = nixpkgs {
      localSystem = stdenv.hostPlatform;
      crossSystem = let
        # Remove an attribute from an attrset by its path
        removeByPath = pathList: set:
          lib.updateManyAttrsByPath (lib.singleton {
            path = lib.init pathList;
            update = old:
              lib.filterAttrs (n: v: n != (lib.last pathList)) old;
          }) set;
      # Can't pass --with-float when building multilib gcc, so need to remove
      # parsed.abi.float.
      in removeByPath [ "parsed" "abi" "float" ] (lib.systems.elaborate {
        config = "arm-none-eabi";
        libc = "newlib-nano";
      });
      overlays = [ optimizeSizeOverlay multilibOverlay ];
    };

    "simulator" = nixpkgs {
      localSystem = stdenv.hostPlatform;
      crossSystem = pkgs.pkgsi686Linux.pkgsStatic.stdenv.hostPlatform;
    };
  }.${platform};
in pkgs'.callPackage ({
  lib, stdenv, callPackage, pkgsBuildBuild, cmake, ninja, makedepend, python3
, dtc, python3Packages, git, nix-prefetch-git, clang-tools, openocd, gdb
, alsa-lib
}: stdenv.mkDerivation {
  pname = "zeus-le";
  version = "0.1.0";

  src = if dev
    then null
    else callPackage ./firmware/west.nix { };

  nativeBuildInputs = [
    cmake
    ninja
    makedepend # babblesim
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
    (pkgsBuildBuild.clang-tools.override {
      llvmPackages = pkgsBuildBuild.llvmPackages_latest;
    })
    openocd
    gdb
  ]);

  buildInputs = lib.optionals (platform == "simulator") [
    alsa-lib
  ];

  env = {
    ZEPHYR_TOOLCHAIN_VARIANT = "cross-compile";
    CROSS_COMPILE = "${stdenv.cc}/bin/${stdenv.cc.targetPrefix}";
  } // lib.optionalAttrs dev {
    # Required by menuconfig
    LOCALE_ARCHIVE = "${pkgsBuildBuild.glibcLocales}/lib/locale/locale-archive";
  };

  cmakeDir = "../zephyr/share/sysbuild";

  cmakeFlags = [
    "-DBOARD=raytac_mdbt53_db_40_nrf5340_cpuapp"
    "-DAPP_DIR=../firmware/central/app"
    # TODO: generate dynamically
    "-DBUILD_VERSION=zephyr-v3.5.0-5533-g68f24837b032"
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
    cp app/zephyr/zephyr.elf "$out/zeus_le_central_app.elf"
    cp net/zephyr/zephyr.elf "$out/zeus_le_central_net.elf"

    runHook postInstall
  '';

  dontFixup = true;
}) { }
