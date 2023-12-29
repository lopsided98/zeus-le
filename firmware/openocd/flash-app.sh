#!/usr/bin/env bash
script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"

"${script_dir}/gdb-app.sh" -batch \
	-ex "load" \
	-ex "monitor reset run" \
	"$@"