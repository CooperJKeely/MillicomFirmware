#ifndef MODE_H
#define MODE_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <zephyr/drivers/adc.h>

// Devicetree validation
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

// Devicetree macro for ADC channels
#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

// Constants for min val read on ADC for capacitor to discharge/recharge
#define SUPER_CAP_THRESHOLD_MV 2000 //190// Need to tune this value
#define MOTOR_CAP_RELEASE_THRESHOLD_MV 3000//310 //need to tune
#define SUPER_CAP_THRESHOLD (SUPER_CAP_THRESHOLD_MV / 2)
#define MOTOR_CAP_RELEASE_THRESHOLD (MOTOR_CAP_RELEASE_THRESHOLD_MV / 6)

#define SUPER_CAP_IDX 0
#define MOTOR_CAP_0_IDX 1
#define MOTOR_CAP_1_IDX 2

// ADC channel count (calculated from devicetree)
#define ADC_CHANNEL_COUNT DT_PROP_LEN(DT_PATH(zephyr_user), io_channels)

typedef enum{
    POWER_LOW_MODE_NONE,
    POWER_MED_MODE_SYNC,
    POWER_HIGH_MODE_SYNC,
    POWER_HIGH_MODE_ADV,
}power_mode_t;

extern volatile power_mode_t current_power_mode;
extern struct k_poll_signal mode_switch_signal;

// ADC MV Buffer - declare with proper size
extern int32_t adc_outputs_mv[ADC_CHANNEL_COUNT];

// ADC channels array
extern const struct adc_dt_spec adc_channels[];

extern struct k_timer adc_timer;

void adc_register_work(struct k_timer*);

void adc_timer_handler(struct k_work *timer_id);

int initMode();

void readADC();

#endif