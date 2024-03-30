#!/usr/bin/env bash
set -euo pipefail

script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
top_dir="${script_dir}/../.."

function usage() {
	echo "Usage: ${0} [-a APP] [-h]

  -a APP        app to simulate (default: central)
  -h            display this help
" >&2
	exit 1
}

app="central"

while getopts "a:h" name; do
	case "${name}" in
		a) app="${OPTARG}" ;;
		h|*) usage ;;
	esac
done
shift $((OPTIND-1))

board="nrf5340bsim_nrf5340_cpuapp"
build_dir="${top_dir}/build/firmware/${app}/app/${board}"
sim="zeus-le"
tmux="zeus-le"
index=0

function cleanup() {
    "${top_dir}/tools/bsim/components/common/stop_bsim.sh" "${sim}"
    # Reset colors
    echo -en "\e[0m"
    # Take down network
    "${top_dir}/tools/net-tools/net-setup.sh" \
        --config "${script_dir}/net.conf" \
        down
}
trap cleanup EXIT

function set_app_cmd() {
  local app="${1}"
  local build_dir="${top_dir}/build/firmware/${app}/app/${board}"
  local d="$((index++))"
  app_cmd=("${build_dir}/app/zephyr/zephyr.exe" "-s=${sim}" "-d=${d}")
}

"${top_dir}/tools/net-tools/net-setup.sh" \
    --config "${script_dir}/net.conf" \
    up

pushd "${top_dir}/tools/bsim/bin"
./bs_2G4_phy_v1 -s="${sim}" -D=2 &
popd

set_app_cmd central
tmux new-session -ds "${tmux}" -- "${app_cmd[@]}"

set_app_cmd audio
tmux split-window -ht "${tmux}" -- "${app_cmd[@]}"

tmux attach-session -t "${tmux}"