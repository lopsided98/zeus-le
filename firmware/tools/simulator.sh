#!/usr/bin/env bash
set -euo pipefail

script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
top_dir="${script_dir}/../.."

function usage() {
	echo "Usage: ${0} [-h]

  -h            display this help
" >&2
	exit 1
}

while getopts "a:h" name; do
	case "${name}" in
		h|*) usage ;;
	esac
done
shift $((OPTIND-1))

board="nrf5340bsim/nrf5340/cpuapp"
sim="zeus-le"
tmux="zeus-le"
index=0

function cleanup() {
    "${top_dir}/tools/bsim/components/common/stop_bsim.sh" "${sim}"
    # Reset colors
    echo -en "\e[0m"
    # Take down network
    "${top_dir}/tools/net-tools/net-setup.sh" \
        --config "${script_dir}/net-central.conf" \
        --iface zeus-central \
        down
    "${top_dir}/tools/net-tools/net-setup.sh" \
        --config "${script_dir}/net-audio.conf" \
        --iface zeus-audio \
        down
}
trap cleanup EXIT

function create_sd_card() {
  local file="${1}"
  dd if=/dev/zero bs=1M count=256 | tr '\0' '\377' > "${file}"
  dd if="${script_dir}/sd_card_mbr.bin" of="${file}" conv=notrunc
  mkfs.vfat --offset 2048 -n Audio "${file}"
}

function set_app_cmd() {
  local app="${1}"
  local build_dir="${top_dir}/build/firmware/${app}/app/${board}"
  local sim_data_dir="${build_dir}/sim-data"
  local d="$((index++))"
  mkdir -p "${sim_data_dir}"
  cd "${sim_data_dir}"
  app_cmd=(
    #"gdb" "--args"
    "${build_dir}/zephyr/zephyr.exe"
    "-s=${sim}"
    "-d=${d}"
    # Babblesim NVMC flash simulator
    "-flash_app=${sim_data_dir}/flash_${app}.bin"
  )
}

"${top_dir}/tools/net-tools/net-setup.sh" \
    --config "${script_dir}/net-central.conf" \
    --iface zeus-central \
    up
"${top_dir}/tools/net-tools/net-setup.sh" \
    --config "${script_dir}/net-audio.conf" \
    --iface zeus-audio \
    up

pushd "${top_dir}/tools/bsim/bin"
./bs_2G4_phy_v1 -s="${sim}" -D=2 &
# d="$((index++))"
# ./bs_device_handbrake -s="${sim}" -d="${d}" -r=1 -pp=10e3 &
popd

set_app_cmd central
tmux new-session -ds "${tmux}" -- "${app_cmd[@]}"
# tmux set-option -t "${tmux}" remain-on-exit on

set_app_cmd audio
if [ ! -f flash.bin ]; then
  create_sd_card flash.bin
fi
tmux split-window -ht "${tmux}" -- "${app_cmd[@]}"

tmux attach-session -t "${tmux}"