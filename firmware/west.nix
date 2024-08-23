
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
        rev = "8074e6b5929333ba5d0e5767879f696da34405c2";
        branchName = "manifest-rev";
        hash = "sha256-ZfdA+Rsap7BxRCmTGq0z5Y31IYVQdlTGcYaEqZqaD5g=";
    };
})

(linkPath {
    link = "tools/bsim/components/ext_nRF_hw_models";
    path = fetchgit {
        url = "https://github.com/BabbleSim/ext_nRF_hw_models.git";
        rev = "3ede17158a9fe85c160e0384c0ad0306e24ee47e";
        branchName = "manifest-rev";
        hash = "sha256-P7Z9V6D1nB4U+MALk3S745UFh5/dbe2vYr33GItpDk4=";
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
        rev = "9351ae1ad44864a49c351f9704f65f43046abeb0";
        branchName = "manifest-rev";
        hash = "sha256-C+FZWqF7az/UuunTTsmYcCG17jElPHduwbFGwSSy88E=";
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
        rev = "04eeb3c3794444122fbeeb3715f4233b0b50cfbb";
        branchName = "manifest-rev";
        hash = "sha256-cl7dKJXj+funFo072aW0/VwQu4AWmMI8wPQ+njzR+hU=";
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
        rev = "91654ddc7ce0da523eb4d6be2171208ae2b8fb35";
        branchName = "manifest-rev";
        hash = "sha256-Hko6cr/TYBDTFGjsQJnlYOByz9iVou4wLBBTU8ZKkn4=";
    };
})

(linkPath {
    link = "modules/lib/hostap";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/hostap";
        rev = "77a4cad575c91f1b234c8d15630f87999881cde2";
        branchName = "manifest-rev";
        hash = "sha256-3FtVhhMssM9n3moUSbvHYBTOTptaR5/kkVMgkhhxpo8=";
    };
})

(linkPath {
    link = "modules/hal/libmetal";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/libmetal";
        rev = "a6851ba6dba8c9e87d00c42f171a822f7a29639b";
        branchName = "manifest-rev";
        hash = "sha256-TvJ8XiIa05lAUDfaGObkEuOjOUn3EzHPDxEVCoeiRBA=";
    };
})

(linkPath {
    link = "modules/crypto/mbedtls";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/mbedtls";
        rev = "2f24831ee13d399ce019c4632b0bcd440a713f7c";
        branchName = "manifest-rev";
        hash = "sha256-mXe04PQocIvBhC2Z4HFVj26Q/qr64I9uBkzVJIlTjoc=";
    };
})

(linkPath {
    link = "tools/net-tools";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/net-tools";
        rev = "7c7a856814d7f27509c8511fef14cec21f7d0c30";
        branchName = "manifest-rev";
        hash = "sha256-T1hnDzDRAGQavm6NNzIFokWsouFJ3rkoAmcZRbvhqQc=";
    };
})

(linkPath {
    link = "modules/lib/open-amp";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/open-amp";
        rev = "76d2168bcdfcd23a9a7dce8c21f2083b90a1e60a";
        branchName = "manifest-rev";
        hash = "sha256-1TenetSRhMPbmM0JKy0TAbBBBsaNTuDF/H3hj7Z3qZM=";
    };
})

(linkPath {
    link = "modules/lib/picolibc";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/picolibc";
        rev = "764ef4e401a8f4c6a86ab723533841f072885a5b";
        branchName = "manifest-rev";
        hash = "sha256-5W/+vorhOP/MD19JYzHJfZTkWMM7sKUQqzTDA/7rxZs=";
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
      export ZEPHYR_MODULES=${lib.escapeShellArg "${placeholder "out"}/firmware;${placeholder "out"}/tools/bsim/components/ext_nRF_hw_models;${placeholder "out"}/modules/hal/cmsis;${placeholder "out"}/modules/fs/fatfs;${placeholder "out"}/modules/hal/nordic;${placeholder "out"}/modules/lib/hostap;${placeholder "out"}/modules/hal/libmetal;${placeholder "out"}/modules/crypto/mbedtls;${placeholder "out"}/modules/lib/open-amp;${placeholder "out"}/modules/lib/picolibc;${placeholder "out"}/modules/crypto/tinycrypt"}
    EOF
  '';
}
