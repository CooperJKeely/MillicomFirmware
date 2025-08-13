#ifndef MODE_H
#define MODE_H

#include <zephyr/kernel.h>
#include <stdbool.h>

typedef enum{
    POWER_LOW_MODE_NONE,
    POWER_MED_MODE_SYNC,
    POWER_HIGH_MODE_SYNC,
    POWER_HIGH_MODE_ADV,
}power_mode_t;

extern volatile power_mode_t current_power_mode;

// kernel signal
extern struct k_poll_signal mode_switch_signal;

void adc_timer_handler(struct k_timer *timer_id);

void start_adc_simulation(void);


#endif