#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int led_boot(void);

int led_sync_started(void);

int led_sync_terminated(void);

int led_record_waiting(void);

int led_record_started(void);

int led_record_stopped(void);

#ifdef __cplusplus
}
#endif