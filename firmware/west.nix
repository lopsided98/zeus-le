
{ lib, runCommand, symlinkJoin, fetchgit }: let
  linkPath = { link, path }: runCommand "west-link" {} ''
    outLink="$out"/${lib.escapeShellArg link}
    mkdir -p "$(dirname "$outLink")"
    ln -s ${lib.escapeShellArg path} "$outLink"
  '';
in symlinkJoin {
  name = "west-workspace";
  paths = [

(linkPath {
    link = "firmware";
    path = "${../firmware}";
})

(linkPath {
    link = "zephyr";
    path = fetchgit {
        url = "https://github.com/lopsided98/zephyr";
        rev = "68f24837b0320f4c97b52e010c6084b8d67fbd33";
        branchName = "manifest-rev";
        hash = "sha256-glh6idGBnGc24nxD3N/Bcv99z4EO0LltAHU+Fp7I728=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_nRF_hw_models";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_nRF_hw_models.git";
        rev = "9b985ea6bc237b6ae06f48eb228f2ac7f6e3b96b";
        branchName = "manifest-rev";
        hash = "sha256-O+S091IKpA7KlOEsTyAqFAwAdyG1nsgMjLZsn7aJQ/Q=";
    };
})

(linkPath {
    link = "tools/west-nix";
    path = fetchgit {
        url = "https://github.com/lopsided98/west-nix";
        rev = "2c09b5de284ab4eb3fa69da1613176e83f84dda0";
        branchName = "manifest-rev";
        hash = "sha256-v4YDB3YZ207csSZaGxQYPZmaWPInzeAg4a5zZTy3T2c=";
    };
})

(linkPath {
    link = "tools/bsim";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/babblesim-manifest";
        rev = "384a091445c57b44ac8cbd18ebd245b47c71db94";
        branchName = "manifest-rev";
        hash = "sha256-bH2eLkhUkw0DwX5TjTIW4kSLksSaoCYiPCLXjD9Rqhg=";
    };
})

(linkPath {
    link = "tools/bsim/components";
    path = fetchgit {
        url = "https://github.com/BabbleSim/base.git";
        rev = "19d62424c0802c6c9fc15528febe666e40f372a1";
        branchName = "manifest-rev";
        hash = "sha256-/Oi/bDeiZQbZJQYGguQ4UadY9jYq3T8d15/cL/I+UnU=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_libPhyComv1";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_libPhyComv1.git";
        rev = "9018113a362fa6c9e8f4b9cab9e5a8f12cc46b94";
        branchName = "manifest-rev";
        hash = "sha256-KvK9vaY9/aRKcrk/Kx1vtYcprQYxjTJKzyTFsL7sUvw=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_phy_v1";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_phy_v1.git";
        rev = "d47c6dd90035b41b14f6921785ccb7b8484868e2";
        branchName = "manifest-rev";
        hash = "sha256-S0DLNVIDGuXVYYGg1uxrRFKP+Gt9MN2H3wDLaxof8wY=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_channel_NtNcable";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_channel_NtNcable.git";
        rev = "20a38c997f507b0aa53817aab3d73a462fff7af1";
        branchName = "manifest-rev";
        hash = "sha256-bh65lHKQ68vBT55Y/TOSC7csNDZ6+1ew+K7eR9Blrxk=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_modem_magic";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_modem_magic.git";
        rev = "cb70771794f0bf6f262aa474848611c68ae8f1ed";
        branchName = "manifest-rev";
        hash = "sha256-/vHHor78Zyqk7lnsbrAtuLQ+J1L+lbFI/MPaChvmHp4=";
    };
})

(linkPath {
    link = "modules/hal/cmsis";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/cmsis";
        rev = "4b96cbb174678dcd3ca86e11e1f24bc5f8726da0";
        branchName = "manifest-rev";
        hash = "sha256-vzCbE69wCMLIjrSz1m8yspgVCto4JSm4Qg5zY/Ozg+k=";
    };
})

(linkPath {
    link = "modules/hal/nordic";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/hal_nordic";
        rev = "4362d0fcdbac80942212bffc999654beac7ab30e";
        branchName = "manifest-rev";
        hash = "sha256-VJNN0xuGHX1s1VnBRraay5GuQo/7gBxJWFsZdQFxNQM=";
    };
})

(linkPath {
    link = "modules/hal/libmetal";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/libmetal";
        rev = "243eed541b9c211a2ce8841c788e62ddce84425e";
        branchName = "manifest-rev";
        hash = "sha256-wCsYllPeDKx4P5gBGEFDcY4QNbMdwrd+5h5GZ8tEsJM=";
    };
})

(linkPath {
    link = "modules/lib/open-amp";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/open-amp";
        rev = "da78aea63159771956fe0c9263f2e6985b66e9d5";
        branchName = "manifest-rev";
        hash = "sha256-7yae0X7zqDPA4CUfGU3LgNBh7qyKGIPLUv6dn/d65Fg=";
    };
})

(linkPath {
    link = "modules/crypto/tinycrypt";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/tinycrypt";
        rev = "3e9a49d2672ec01435ffbf0d788db6d95ef28de0";
        branchName = "manifest-rev";
        hash = "sha256-5gtZbZNx+D/EUkyYk7rPtcxBZaNs4IFGTP/7IXzCoqU=";
    };
})

  ];
  postBuild = ''
    cat << EOF > "$out/.zephyr-env"
      export ZEPHYR_BASE=${lib.escapeShellArg "${placeholder "out"}/zephyr"}
      export ZEPHYR_MODULES=${lib.escapeShellArg "${placeholder "out"}/tools/bsim/components/ext_nRF_hw_models;${placeholder "out"}/modules/hal/cmsis;${placeholder "out"}/modules/hal/nordic;${placeholder "out"}/modules/hal/libmetal;${placeholder "out"}/modules/lib/open-amp;${placeholder "out"}/modules/crypto/tinycrypt"}
    EOF
  '';
}
