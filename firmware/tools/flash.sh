#!/usr/bin/env bash
set -euo pipefail

script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

function usage() {
	echo "Usage: ${0} [-a APP] [-b [-h]

  -a APP        app to flash ([central], audio)
  -b BOARD      board to flash (default: zeus_le)
  -c CPU        CPU to flash (app, net, [both])
  -h            display this help
" >&2
	exit 1
}

app="central"
board="zeus_le"
cpu="both"

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
	app|net|both) ;;
	*)
		echo "error: unknown CPU: ${cpu}" >&2
		usage
	;;
esac

if [ "${cpu}" == app ] || [ "${cpu}" == both ]; then
	# Reset after flashing app CPU if not also flashing net CPU
	app_reset=()
	if [ "${cpu}" == app ]; then
		app_reset=(-ex "monitor reset run")
	fi

	"${script_dir}/gdb.sh" \
		-a "${app}" \
		-b "${board}" \
		-c app \
		-- \
		-batch \
		-ex "load" \
		"${app_reset[@]}" \
		"$@"
fi

if [ "${cpu}" == net ] || [ "${cpu}" == both ]; then
	"${script_dir}/gdb.sh" \
		-a "${app}" \
		-b "${board}" \
		-c net \
		-- \
		-batch \
		-ex "load" \
		-ex "monitor reset run" \
		"$@"
fi