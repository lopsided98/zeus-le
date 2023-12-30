{ lib, linkFarm, fetchgit }: (linkFarm "west-workspace" [

{
    name = "firmware";
    path = "${../firmware}";
}

{
    name = "zephyr";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/zephyr";
        rev = "aa4416624dceef29ec9ca78820c9fe5d3868f62c";
        branchName = "manifest-rev";
        hash = "sha256-Y9g1d1jrKrh5H7CTHtc6efLm8/Pa3eE97BRzYHeZEIo=";
    };
}

{
    name = "tools/west-nix";
    path = fetchgit {
        url = "https://github.com/lopsided98/west-nix";
        rev = "bde036d3aef61ef1d0cff2cf7226bc9e8d2b23ae";
        branchName = "manifest-rev";
        hash = "sha256-cOEYcEFcqtWowT4dG3hn+kunIvE2eXgUcb8LETgxWac=";
    };
}

{
    name = "modules/hal/cmsis";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/cmsis";
        rev = "4b96cbb174678dcd3ca86e11e1f24bc5f8726da0";
        branchName = "manifest-rev";
        hash = "sha256-vzCbE69wCMLIjrSz1m8yspgVCto4JSm4Qg5zY/Ozg+k=";
    };
}

{
    name = "modules/hal/nordic";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/hal_nordic";
        rev = "b9633ecea67bf52925d4c61455046223b46402b1";
        branchName = "manifest-rev";
        hash = "sha256-BdPhDjSOwPLHuO6UKYWzZfUK4DHnfq1spKTsopVypkI=";
    };
}

{
    name = "modules/hal/libmetal";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/libmetal";
        rev = "243eed541b9c211a2ce8841c788e62ddce84425e";
        branchName = "manifest-rev";
        hash = "sha256-wCsYllPeDKx4P5gBGEFDcY4QNbMdwrd+5h5GZ8tEsJM=";
    };
}

{
    name = "modules/lib/open-amp";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/open-amp";
        rev = "da78aea63159771956fe0c9263f2e6985b66e9d5";
        branchName = "manifest-rev";
        hash = "sha256-7yae0X7zqDPA4CUfGU3LgNBh7qyKGIPLUv6dn/d65Fg=";
    };
}

{
    name = "modules/crypto/tinycrypt";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/tinycrypt";
        rev = "3e9a49d2672ec01435ffbf0d788db6d95ef28de0";
        branchName = "manifest-rev";
        hash = "sha256-5gtZbZNx+D/EUkyYk7rPtcxBZaNs4IFGTP/7IXzCoqU=";
    };
}
])

.overrideAttrs ({ buildCommand, ... }: {
    buildCommand = buildCommand + ''
        cat << EOF > .zephyr-env
          export ZEPHYR_BASE='${placeholder "out"}/zephyr'
          export ZEPHYR_MODULES='${placeholder "out"}/modules/hal/cmsis;${placeholder "out"}/modules/hal/nordic;${placeholder "out"}/modules/hal/libmetal;${placeholder "out"}/modules/lib/open-amp;${placeholder "out"}/modules/crypto/tinycrypt'
        EOF
    '';
})
