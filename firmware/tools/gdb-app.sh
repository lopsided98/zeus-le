#!/usr/bin/env bash
script_dir="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
build_dir="${script_dir}/../../build"

arm-none-eabi-gdb "${build_dir}/app/zephyr/zephyr.elf" \
	-ex "target extended-remote :3333" \
	"$@"