// SPDX-License-Identifier: GPL-3.0-or-later
#include <inttypes.h>
#include <nrfx_egu.h>
#include <nrfx_timer.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "hci_ipc.h"
#include "zeus/sync.h"

// Zephyr internal headers, order is important
// clang-format off
#include <bluetooth/controller/ll_sw/pdu_df.h>
#include <bluetooth/controller/ll_sw/nordic/lll/pdu_vendor.h>
#include <bluetooth/controller/ll_sw/pdu.h>
// clang-format on

LOG_MODULE_REGISTER(central_net);

#define PACKET_TIMER_EGU_IDX 0
#define PACKET_TIMER_IDX 2

struct packet_timer {
    uint8_t seq;
    nrfx_timer_t timer;
    struct ipc_ept ept;
} packet_timer;

static const struct ipc_ept_cfg packet_timer_ept_cfg = {
    .name = "packet_timer",
};

static void packet_timer_isr(uint8_t event_idx, void* context) {
    struct packet_timer* t = (struct packet_timer*)context;

    const struct pdu_adv* pdu = (const struct pdu_adv*)NRF_RADIO->PACKETPTR;
    if (!pdu) {
        LOG_WRN("Null PDU");
        return;
    }
    // Check PDU type, but this is not enough because all extended advertising
    // packets share the same type
    if (pdu->type != PDU_ADV_TYPE_AUX_SYNC_IND) return;
    // Make sure there is no extended advertising header. All extended
    // advertising packets except the periodic packet have a header.
    if (pdu->adv_ext_ind.ext_hdr_len != 0) return;

    const struct zeus_packet_timer_msg msg = {
        .seq = t->seq++,
        .timer = nrfx_timer_capture_get(&t->timer, NRF_TIMER_CC_CHANNEL0),
    };

    ipc_service_send(&t->ept, &msg, sizeof(msg));
}

static int packet_timer_init(void) {
    int err;
    nrfx_err_t nerr;
    const struct device* ipc = DEVICE_DT_GET(DT_NODELABEL(ipc0));

    err = ipc_service_open_instance(ipc);
    if (err < 0 && err != -EALREADY) {
        LOG_ERR("failed to initialize IPC (err %d)\n", err);
        return err;
    }
    err = ipc_service_register_endpoint(ipc, &packet_timer.ept,
                                        &packet_timer_ept_cfg);
    if (err < 0) {
        LOG_ERR("failed to register IPC endpoint (err %d)\n", err);
        return err;
    }

    packet_timer.timer = (nrfx_timer_t)NRFX_TIMER_INSTANCE(PACKET_TIMER_IDX);

    // Setup 32-bit 16 MHz timer to capture on radio end event
    nerr = nrfx_timer_init(&packet_timer.timer,
                           &(nrfx_timer_config_t){
                               .frequency = 16000000,
                               .mode = NRF_TIMER_MODE_TIMER,
                               .bit_width = NRF_TIMER_BIT_WIDTH_32,
                           },
                           NULL);
    NRFX_ASSERT(NRFX_SUCCESS == nerr);

    // Subscribe to radio end event through existing DPPI channel configured by
    // BLE driver
    nrf_timer_subscribe_set(packet_timer.timer.p_reg, NRF_TIMER_TASK_CAPTURE0,
                            HAL_SW_SWITCH_TIMER_CLEAR_PPI);

    nrfx_egu_t egu = NRFX_EGU_INSTANCE(PACKET_TIMER_EGU_IDX);

    nrfx_egu_init(&egu, NRFX_EGU_DEFAULT_CONFIG_IRQ_PRIORITY, packet_timer_isr,
                  &packet_timer);
    IRQ_DIRECT_CONNECT(
        NRFX_IRQ_NUMBER_GET(NRF_EGU_INST_GET(PACKET_TIMER_EGU_IDX)), 5,
        NRFX_EGU_INST_HANDLER_GET(PACKET_TIMER_EGU_IDX), 0);

    // Use EGU to fire interrupt when packet is transmitted
    nrf_egu_subscribe_set(egu.p_reg, NRF_EGU_TASK_TRIGGER0,
                          HAL_SW_SWITCH_TIMER_CLEAR_PPI);
    nrfx_egu_int_enable(&egu, NRF_EGU_INT_TRIGGERED0);

    // Start the timer
    nrfx_timer_enable(&packet_timer.timer);

    return 0;
}

int main(void) {
    int err;

    err = packet_timer_init();
    if (err) {
        return err;
    }

    err = hci_ipc_init();
    if (err) {
        return err;
    }

    LOG_INF("Booted");

    return 0;
}