#!/usr/bin/env bash
set -eu

script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
top_dir="${script_dir}/../.."
build_dir="${top_dir}/build"

sim="zeus_le"

function cleanup() {
    "${top_dir}/tools/bsim/components/common/stop_bsim.sh" "${sim}"
    # Reset colors
    echo -en "\e[0m"
    # Take down network
    "${top_dir}/tools/net-tools/net-setup.sh" down
}
trap cleanup EXIT

"${top_dir}/tools/net-tools/net-setup.sh" up

pushd "${top_dir}/tools/bsim/bin"
./bs_2G4_phy_v1 -s="${sim}" -D=2 &
./bs_device_handbrake -s="${sim}" -d=0 -r=1 &
popd

"${build_dir}/app/zephyr/zephyr.exe" -s="${sim}" -d=1 "${@}"