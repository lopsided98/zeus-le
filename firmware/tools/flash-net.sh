#!/usr/bin/env bash
script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

"${script_dir}/gdb-net.sh" -batch \
	-ex "load" \
	-ex "monitor reset run" \
	"$@"