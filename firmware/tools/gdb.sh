#!/usr/bin/env bash
script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
top_dir="${script_dir}/../.."

function usage() {
	echo "Usage: ${0} [-a APP] [-b [-h]

  -a APP        app to debug (default: central)
  -b BOARD      board to debug (default: raytac_mdbt53_db_40)
  -c CPU        CPU to debug (default: app)
  -h            display this help
" >&2
	exit 1
}

app="central"
board="raytac_mdbt53_db_40"
cpu="app"

while getopts "a:b:c:h" name; do
	case "${name}" in
		a) app="${OPTARG}" ;;
		b) board="${OPTARG}" ;;
		c) cpu="${OPTARG}" ;;
		h|*) usage ;;
	esac
done
shift $((OPTIND-1))

case "${cpu}" in
	app) port=3333 ;;
	net) port=3334 ;;
	*)
		echo "error: unknown CPU: ${cpu}" >&2
		usage
	;;
esac

build_dir="${top_dir}/build/firmware/${app}/app/${board}/nrf5340/cpuapp"

arm-none-eabi-gdb "${build_dir}/${cpu}/zephyr/zephyr.elf" \
	-ex "target extended-remote :${port}" \
	"$@"