{ lib, linkFarm, fetchgit, writeText }: (linkFarm "west-workspace" [

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
        hash = "sha256-Ec1B9jSBxkIKBBZjKzdXKkXsAbey50X4S3oDo55BUOk=";
        leaveDotGit = true;
    };
}

{
    name = "tools/west-nix";
    path = fetchgit {
        url = "https://github.com/lopsided98/west-nix";
        rev = "179b1bc35fca6e5aa2971557285292c027ca105a";
        branchName = "manifest-rev";
        hash = "sha256-B5VWa8AckLkwjGyy8Lngu/4FOJFNpLVHaoT3TvWUJTw=";
        leaveDotGit = true;
    };
}

{
    name = "modules/hal/cmsis";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/cmsis";
        rev = "5a00331455dd74e31e80efa383a489faea0590e3";
        branchName = "manifest-rev";
        hash = "sha256-06vNDFbmmVsWkzE/Zv+6cAW1nTIxR2Gizt+ZeaM86yI=";
        leaveDotGit = true;
    };
}

{
    name = "modules/hal/nordic";
    path = fetchgit {
        url = "https://github.com/zephyrproject-rtos/hal_nordic";
        rev = "d054a315eb888ba70e09e5f6decd4097b0276d1f";
        branchName = "manifest-rev";
        hash = "sha256-3aONqZ0avs5Inti+Z4g98256s9lp9AgsiH0v8s2CKdc=";
        leaveDotGit = true;
    };
}

]).overrideAttrs ({ buildCommand, ... }: {
    buildCommand = buildCommand + ''
        mkdir -p .west
        cat << EOF > .west/config
        [manifest]
        path = firmware
        file = west.yml
        EOF
    '';
})
