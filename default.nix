{ nixpkgs, pkgs, lib, stdenv
, board ? "zeus_le"
, firmware ? "audio"
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

  platform = {
    "zeus_le" = "arm";
    "nrf5340bsim" = "simulator";
  }.${board} or (throw "unsupported board: ${board}");

  pkgs' = {
    "arm" = nixpkgs {
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
      crossSystem = pkgs.pkgsi686Linux.stdenv.hostPlatform;
    };
  }.${platform};
in pkgs'.callPackage ({
  lib, stdenv, linkFarm, makeStaticLibraries, callPackage, buildPackages
, llvmPackages_latest, cmake, ninja, makedepend, python3, dtc, python3Packages
, git, bash-completion, nix-prefetch-git, clang-tools, gdb, openocd, tmux, glibc
, alsa-lib
}: let
  # Zephyr host toolchain only looks for unprefixed tools
  unprefixed-cc = linkFarm "host-cc" (builtins.map (name: {
    name = "bin/${name}";
    path = "${stdenv.cc}/bin/${stdenv.cc.targetPrefix}${name}";
  }) [
    "ar"
    "g++"
    "gcc"
    "ld.bfd"
    "nm" # Needed by BabbleSim
    "readelf"
  ]);

  # Make a package that can be linked against the Zephyr posix arch for the
  # simulator. This requires enabling static libraries and disabling PIE.
  makeZephyrPackage = pkg: (pkg.override {
    stdenv = makeStaticLibraries stdenv;
  }).overrideAttrs ({ env ? {}, ...}: {
    env = env // {
      # undefined reference to `__x86.get_pc_thunk.ax'
      NIX_CFLAGS_COMPILE = (env.NIX_CFLAGS_COMPILE or "") + " -fno-pie";
    };
  });
in stdenv.mkDerivation {
  pname = "zeus-le";
  version = "0.1.0";

  src = if dev
    then null
    else callPackage ./firmware/west.nix { };

  depsBuildBuild = lib.optionals dev [
    llvmPackages_latest.clang-tools
  ] ++ lib.optionals (platform == "arm") [
    # Splicing is buggy and tries to eval for target platform
    openocd
  ];

  nativeBuildInputs = [
    unprefixed-cc
    cmake
    ninja
    makedepend # babblesim
    python3
    dtc
  ] ++ (with python3Packages; [
    packaging
    pyelftools
    pykwalify
    pyyaml
  ]) ++ lib.optionals dev ([
    bash-completion
    git
    nix-prefetch-git
    gdb
  ] ++ (with python3Packages; [
    west
    requests # west blobs
    anytree # ram_report
  ]) ++ lib.optionals (platform == "simulator") [
    tmux
  ]);

  buildInputs = lib.optionals (platform == "simulator") [
    (makeZephyrPackage alsa-lib)
  ];

  hardeningDisable = [ "fortify" ];

  env = {
    ZEPHYR_TOOLCHAIN_VARIANT = "cross-compile";
    CROSS_COMPILE = "${stdenv.cc}/bin/${stdenv.cc.targetPrefix}";
  } // lib.optionalAttrs dev {
    # Required by menuconfig
    LOCALE_ARCHIVE = "${buildPackages.glibcLocales}/lib/locale/locale-archive";
  };

  shellHook = ''
    source '${buildPackages.bash-completion}/etc/profile.d/bash_completion.sh'
  '';

  cmakeDir = "../zephyr/share/sysbuild";

  cmakeFlags = [
    "-DBOARD=${board}/nrf5340/cpuapp"
    "-DAPP_DIR=../firmware/${firmware}/app"
    # TODO: generate dynamically
    "-DBUILD_VERSION=zephyr-v3.6.0-9291-g8074e6b59293"
  ];

  preConfigure = ''
    export XDG_CACHE_HOME="$TMPDIR"

    source .zephyr-env
  '' + lib.optionalString (platform == "simulator") ''
    # Must be absolute paths
    cmakeFlagsArray+=(
      "-DBSIM_COMPONENTS_PATH=$(pwd)/tools/bsim/components"
      "-DBSIM_OUT_PATH=$(pwd)/tools/bsim"
    )
    make -C tools/bsim CC="$CC" AR="$AR" -j "$NIX_BUILD_CORES" everything
  '';

  installPhase = ''
    runHook preInstall

    mkdir -p "$out"
    cp app/zephyr/zephyr.elf "$out/zeus_le_${firmware}_app.elf"
    cp net/zephyr/zephyr.elf "$out/zeus_le_${firmware}_net.elf"

    runHook postInstall
  '';

  dontFixup = true;
}) { }
