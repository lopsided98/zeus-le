if("${SB_CONFIG_NET_CORE_BOARD}" STREQUAL "")
	message(FATAL_ERROR
	"Target ${BOARD} not supported for this app. "
	"There is no remote board selected in Kconfig.sysbuild")
endif()

set(REMOTE_APP net)

ExternalZephyrProject_Add(
	APPLICATION ${REMOTE_APP}
	SOURCE_DIR  ${APP_DIR}/../${REMOTE_APP}
	BOARD       ${SB_CONFIG_NET_CORE_BOARD}
)

native_simulator_set_child_images(${DEFAULT_IMAGE} ${REMOTE_APP})

native_simulator_set_final_executable(${DEFAULT_IMAGE})
