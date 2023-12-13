{ lib, linkFarm, fetchgit }: (linkFarm "west-workspace" [

{
    name = "firmware";
    path = "${../firmware}";
}

{
    name = "zephyr";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/zephyr";
        rev = "v3.5.0";
        branchName = "manifest-rev";
        hash = "sha256-AKmTkHA+/1BM/qMtLEMF0NDUHCqo0A2e5LKLhRRLilU=";
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
        rev = "5a00331455dd74e31e80efa383a489faea0590e3";
        branchName = "manifest-rev";
        hash = "sha256-1oCeT681nFDbCyhp0mErktuoj3YtFDzP5dLYdWz0+AM=";
    };
}

{
    name = "modules/hal/nordic";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/hal_nordic";
        rev = "d054a315eb888ba70e09e5f6decd4097b0276d1f";
        branchName = "manifest-rev";
        hash = "sha256-0rSZyKvK0iE4qPtPlRfF0IPndi/lLfJk4WJRbwLx9no=";
    };
}

{
    name = "modules/hal/libmetal";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/libmetal";
        rev = "b91611a6f47dd29fb24c46e5621e797557f80ec6";
        branchName = "manifest-rev";
        hash = "sha256-n0mZzV62tZI27Y6dftMPWAtMaJQDuB9ju9THrr55QF4=";
    };
}

{
    name = "modules/lib/open-amp";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/open-amp";
        rev = "42b7c577714b8f22ce82a901e19c1814af4609a8";
        branchName = "manifest-rev";
        hash = "sha256-iZtC8uXGapFPNU2XNWjfc6DmuaqkUiZo6H/swciOATI=";
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
