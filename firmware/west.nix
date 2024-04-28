
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
        rev = "66593873f98ef1a503b96521d699d2db85c6571e";
        branchName = "manifest-rev";
        hash = "sha256-8U5Byn6fLfHk+yIaaIKWGDSy+FrIX3LbhNjFmrKHAgs=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_nRF_hw_models";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_nRF_hw_models.git";
        rev = "e55aaff1359d8854856f910ae0ba0acf8ed19a37";
        branchName = "manifest-rev";
        hash = "sha256-8RylIgzal8lHoqRA581iTGF0BwACoY4u3QPZvGAaruQ=";
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
        rev = "68f6282c6a7f54641b75f5f9fc953c85e272a983";
        branchName = "manifest-rev";
        hash = "sha256-VF1VPfeQCyQd89DGbQGbePNHLbBo+lWfkjPNj9TSqYo=";
    };
})

(linkPath {
    link = "tools/bsim/components";
    path = fetchgit {
        url = "https://github.com/BabbleSim/base";
        rev = "4bd907be0b2abec3b31a23fd8ca98db2a07209d2";
        branchName = "manifest-rev";
        hash = "sha256-W16Zb7FIHs+/T20WM/usITwgnht1SUrpLiXdSFp8YIw=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_libPhyComv1";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_libPhyComv1";
        rev = "93f5eba512c438b0c9ebc1b1a947517c865b3643";
        branchName = "manifest-rev";
        hash = "sha256-2t/EU5T2uRao90nRiipaNf0adX4ittlHuugu8zL4NNc=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_phy_v1";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_phy_v1";
        rev = "d8302b8d51409b9e717a1a0ba6b443d3b5552a6c";
        branchName = "manifest-rev";
        hash = "sha256-Tb0su9q5wTCH8M0RJ99gGUwVM2GCl1NimGUe7YSkPug=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_channel_NtNcable";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_channel_NtNcable";
        rev = "20a38c997f507b0aa53817aab3d73a462fff7af1";
        branchName = "manifest-rev";
        hash = "sha256-bh65lHKQ68vBT55Y/TOSC7csNDZ6+1ew+K7eR9Blrxk=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_2G4_modem_magic";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_2G4_modem_magic";
        rev = "edfcda2d3937a74be0a59d6cd47e0f50183453da";
        branchName = "manifest-rev";
        hash = "sha256-q/GbN+5JNx3a1h1xttnRU8P3xxcB0wB4F6otnQD1F8I=";
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
    link = "modules/fs/fatfs";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/fatfs";
        rev = "427159bf95ea49b7680facffaa29ad506b42709b";
        branchName = "manifest-rev";
        hash = "sha256-5l3hJazG8BoLZ0QxfSFc98uKArIGAFD1P8D6tsKMRt4=";
    };
})

(linkPath {
    link = "modules/hal/nordic";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/hal_nordic";
        rev = "827224f29e99972cd3b999dba913ae4649790671";
        branchName = "manifest-rev";
        hash = "sha256-Kcol3sMPSEuza2CPPYIaJptp76hUrzRkSWtrjRfjAbQ=";
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
    link = "tools/net-tools";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/net-tools";
        rev = "cd2eb1858a1570b49241e18fc1e1cd849a450af2";
        branchName = "manifest-rev";
        hash = "sha256-EkisIg3DJBgULxyIVB7P1muHVlZCqh50KbbV/dfCnlY=";
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
        rev = "1012a3ebee18c15ede5efc8332ee2fc37817670f";
        branchName = "manifest-rev";
        hash = "sha256-JQmkN+ircGU5Ald1+Q4lysuxhZLg2mJpNQ92+agMoCQ=";
    };
})

  ];
  postBuild = ''
    cat << EOF > "$out/.zephyr-env"
      export ZEPHYR_BASE=${lib.escapeShellArg "${placeholder "out"}/zephyr"}
      export ZEPHYR_MODULES=${lib.escapeShellArg "${placeholder "out"}/tools/bsim/components/ext_nRF_hw_models;${placeholder "out"}/modules/hal/cmsis;${placeholder "out"}/modules/fs/fatfs;${placeholder "out"}/modules/hal/nordic;${placeholder "out"}/modules/hal/libmetal;${placeholder "out"}/modules/lib/open-amp;${placeholder "out"}/modules/crypto/tinycrypt"}
    EOF
  '';
}
