#!/usr/bin/env bash
script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
top_dir="${script_dir}/../.."
build_dir="${top_dir}/build"

function cleanup() {
    # Reset colors
    echo -en "\e[0m"
}
trap cleanup EXIT

pushd "${top_dir}/tools/bsim/bin"
./bs_2G4_phy_v1 -s=sim -D=1 &
popd

"${build_dir}/app/zephyr/zephyr.exe" -s=sim -d=0