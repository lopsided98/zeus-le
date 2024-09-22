
{ lib, runCommand, lndir, fetchgit, fetchurl }:

runCommand "west-workspace" {
nativeBuildInputs = [ lndir ];
} ''

    mkdir -p "$out"/'firmware'
    lndir -silent \
        ${lib.escapeShellArg ("${../firmware}")} \
        "$out"/'firmware'

    mkdir -p "$out"/'zephyr'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/lopsided98/zephyr";
            rev = "a6f4d4ef93b7f92cc87fa1b70df341e1b9e78939";
            branchName = "manifest-rev";
            hash = "sha256-mqOTJTYV113xof7bmb1RAsbfxF2R9uWF9FlZ3iJTexo=";
        })} \
        "$out"/'zephyr'

    mkdir -p "$out"/'tools/bsim/components/ext_nRF_hw_models'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/BabbleSim/ext_nRF_hw_models.git";
            rev = "3ede17158a9fe85c160e0384c0ad0306e24ee47e";
            branchName = "manifest-rev";
            hash = "sha256-P7Z9V6D1nB4U+MALk3S745UFh5/dbe2vYr33GItpDk4=";
        })} \
        "$out"/'tools/bsim/components/ext_nRF_hw_models'

    mkdir -p "$out"/'tools/west-nix'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/lopsided98/west-nix";
            rev = "3f31c82804e217842dc40e21ab8f95a037dfc8f7";
            branchName = "manifest-rev";
            hash = "sha256-l30Ib09LT0+kFhq6DYqIWlLznRP0ZMxgzEzU6L2d1nQ=";
        })} \
        "$out"/'tools/west-nix'

    mkdir -p "$out"/'tools/bsim'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/babblesim-manifest";
            rev = "9351ae1ad44864a49c351f9704f65f43046abeb0";
            branchName = "manifest-rev";
            hash = "sha256-C+FZWqF7az/UuunTTsmYcCG17jElPHduwbFGwSSy88E=";
        })} \
        "$out"/'tools/bsim'

    mkdir -p "$out"/'tools/bsim/components'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/BabbleSim/base";
            rev = "4bd907be0b2abec3b31a23fd8ca98db2a07209d2";
            branchName = "manifest-rev";
            hash = "sha256-W16Zb7FIHs+/T20WM/usITwgnht1SUrpLiXdSFp8YIw=";
        })} \
        "$out"/'tools/bsim/components'

    mkdir -p "$out"/'tools/bsim/components/ext_2G4_libPhyComv1'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/BabbleSim/ext_2G4_libPhyComv1";
            rev = "93f5eba512c438b0c9ebc1b1a947517c865b3643";
            branchName = "manifest-rev";
            hash = "sha256-2t/EU5T2uRao90nRiipaNf0adX4ittlHuugu8zL4NNc=";
        })} \
        "$out"/'tools/bsim/components/ext_2G4_libPhyComv1'

    mkdir -p "$out"/'tools/bsim/components/ext_2G4_phy_v1'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/BabbleSim/ext_2G4_phy_v1";
            rev = "04eeb3c3794444122fbeeb3715f4233b0b50cfbb";
            branchName = "manifest-rev";
            hash = "sha256-cl7dKJXj+funFo072aW0/VwQu4AWmMI8wPQ+njzR+hU=";
        })} \
        "$out"/'tools/bsim/components/ext_2G4_phy_v1'

    mkdir -p "$out"/'tools/bsim/components/ext_2G4_channel_NtNcable'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/BabbleSim/ext_2G4_channel_NtNcable";
            rev = "20a38c997f507b0aa53817aab3d73a462fff7af1";
            branchName = "manifest-rev";
            hash = "sha256-bh65lHKQ68vBT55Y/TOSC7csNDZ6+1ew+K7eR9Blrxk=";
        })} \
        "$out"/'tools/bsim/components/ext_2G4_channel_NtNcable'

    mkdir -p "$out"/'tools/bsim/components/ext_2G4_modem_magic'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/BabbleSim/ext_2G4_modem_magic";
            rev = "edfcda2d3937a74be0a59d6cd47e0f50183453da";
            branchName = "manifest-rev";
            hash = "sha256-q/GbN+5JNx3a1h1xttnRU8P3xxcB0wB4F6otnQD1F8I=";
        })} \
        "$out"/'tools/bsim/components/ext_2G4_modem_magic'

    mkdir -p "$out"/'modules/hal/cmsis'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/cmsis";
            rev = "4b96cbb174678dcd3ca86e11e1f24bc5f8726da0";
            branchName = "manifest-rev";
            hash = "sha256-vzCbE69wCMLIjrSz1m8yspgVCto4JSm4Qg5zY/Ozg+k=";
        })} \
        "$out"/'modules/hal/cmsis'

    mkdir -p "$out"/'modules/fs/fatfs'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/fatfs";
            rev = "427159bf95ea49b7680facffaa29ad506b42709b";
            branchName = "manifest-rev";
            hash = "sha256-5l3hJazG8BoLZ0QxfSFc98uKArIGAFD1P8D6tsKMRt4=";
        })} \
        "$out"/'modules/fs/fatfs'

    mkdir -p "$out"/'modules/hal/nordic'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/hal_nordic";
            rev = "91654ddc7ce0da523eb4d6be2171208ae2b8fb35";
            branchName = "manifest-rev";
            hash = "sha256-Hko6cr/TYBDTFGjsQJnlYOByz9iVou4wLBBTU8ZKkn4=";
        })} \
        "$out"/'modules/hal/nordic'

    mkdir -p "$out"/'modules/lib/hostap'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/hostap";
            rev = "77a4cad575c91f1b234c8d15630f87999881cde2";
            branchName = "manifest-rev";
            hash = "sha256-3FtVhhMssM9n3moUSbvHYBTOTptaR5/kkVMgkhhxpo8=";
        })} \
        "$out"/'modules/lib/hostap'

    mkdir -p "$out"/'modules/hal/libmetal'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/libmetal";
            rev = "a6851ba6dba8c9e87d00c42f171a822f7a29639b";
            branchName = "manifest-rev";
            hash = "sha256-TvJ8XiIa05lAUDfaGObkEuOjOUn3EzHPDxEVCoeiRBA=";
        })} \
        "$out"/'modules/hal/libmetal'

    mkdir -p "$out"/'modules/crypto/mbedtls'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/mbedtls";
            rev = "2f24831ee13d399ce019c4632b0bcd440a713f7c";
            branchName = "manifest-rev";
            hash = "sha256-IgQFVvsXcAbxvVcrv3U/eEt4fCo+ramzYswI97BAExE=";
        })} \
        "$out"/'modules/crypto/mbedtls'

    mkdir -p "$out"/'tools/net-tools'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/net-tools";
            rev = "7c7a856814d7f27509c8511fef14cec21f7d0c30";
            branchName = "manifest-rev";
            hash = "sha256-T1hnDzDRAGQavm6NNzIFokWsouFJ3rkoAmcZRbvhqQc=";
        })} \
        "$out"/'tools/net-tools'

    mkdir -p "$out"/'modules/lib/open-amp'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/open-amp";
            rev = "76d2168bcdfcd23a9a7dce8c21f2083b90a1e60a";
            branchName = "manifest-rev";
            hash = "sha256-1TenetSRhMPbmM0JKy0TAbBBBsaNTuDF/H3hj7Z3qZM=";
        })} \
        "$out"/'modules/lib/open-amp'

    mkdir -p "$out"/'modules/lib/picolibc'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/picolibc";
            rev = "764ef4e401a8f4c6a86ab723533841f072885a5b";
            branchName = "manifest-rev";
            hash = "sha256-5W/+vorhOP/MD19JYzHJfZTkWMM7sKUQqzTDA/7rxZs=";
        })} \
        "$out"/'modules/lib/picolibc'

    mkdir -p "$out"/'modules/crypto/tinycrypt'
    lndir -silent \
        ${lib.escapeShellArg (
        fetchgit {
            url = "https://github.com/zephyrproject-rtos/tinycrypt";
            rev = "1012a3ebee18c15ede5efc8332ee2fc37817670f";
            branchName = "manifest-rev";
            hash = "sha256-JQmkN+ircGU5Ald1+Q4lysuxhZLg2mJpNQ92+agMoCQ=";
        })} \
        "$out"/'modules/crypto/tinycrypt'

    mkdir -p "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/default'
    ln -s \
        ${lib.escapeShellArg (
        fetchurl {
            url = "https://github.com/nrfconnect/sdk-nrfxlib/raw/51fc239e2b677bcadb720eb7af9efc98553feb4e/nrf_wifi/fw_bins/default/nrf70.bin";
            hash = "sha256:937dd2c6bd7250da89b84b9093a3726ff42a32070a8a1ae822ffd69e05f32b19";
        })} \
        "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/default/nrf70.bin'

    mkdir -p "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/scan_only'
    ln -s \
        ${lib.escapeShellArg (
        fetchurl {
            url = "https://github.com/nrfconnect/sdk-nrfxlib/raw/51fc239e2b677bcadb720eb7af9efc98553feb4e/nrf_wifi/fw_bins/scan_only/nrf70.bin";
            hash = "sha256:34c598de69821bdbb92541bfec8ba1fdabb458f5287a0cbb6d7178bc72958b7a";
        })} \
        "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/scan_only/nrf70.bin'

    mkdir -p "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/radio_test'
    ln -s \
        ${lib.escapeShellArg (
        fetchurl {
            url = "https://github.com/nrfconnect/sdk-nrfxlib/raw/51fc239e2b677bcadb720eb7af9efc98553feb4e/nrf_wifi/fw_bins/radio_test/nrf70.bin";
            hash = "sha256:707c40d4c741a79818dfb99574fe02be6d8d8f46bc557577b16e95ca6a443acf";
        })} \
        "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/radio_test/nrf70.bin'

    mkdir -p "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/system_with_raw'
    ln -s \
        ${lib.escapeShellArg (
        fetchurl {
            url = "https://github.com/nrfconnect/sdk-nrfxlib/raw/51fc239e2b677bcadb720eb7af9efc98553feb4e/nrf_wifi/fw_bins/system_with_raw/nrf70.bin";
            hash = "sha256:780f74e39204ab319dc309bfaef9938a984cf33b93d8fcd08d0dff4a7ab15eb9";
        })} \
        "$out"/'modules/hal/nordic/zephyr/blobs/wifi_fw_bins/system_with_raw/nrf70.bin'

    cat << EOF > "$out/.zephyr-env"
    export ZEPHYR_BASE=${lib.escapeShellArg "${placeholder "out"}/zephyr"}
    export ZEPHYR_MODULES=${lib.escapeShellArg "${placeholder "out"}/firmware;${placeholder "out"}/tools/bsim/components/ext_nRF_hw_models;${placeholder "out"}/modules/hal/cmsis;${placeholder "out"}/modules/fs/fatfs;${placeholder "out"}/modules/hal/nordic;${placeholder "out"}/modules/lib/hostap;${placeholder "out"}/modules/hal/libmetal;${placeholder "out"}/modules/crypto/mbedtls;${placeholder "out"}/modules/lib/open-amp;${placeholder "out"}/modules/lib/picolibc;${placeholder "out"}/modules/crypto/tinycrypt"}
    EOF
''
