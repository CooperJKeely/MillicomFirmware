#include "mode.h"
#include <stdlib.h>

// Global variable definitions
volatile power_mode_t current_power_mode = POWER_LOW_MODE_NONE; 
struct k_poll_signal mode_switch_signal;
int32_t adc_outputs_mv[ADC_CHANNEL_COUNT];  // Define the ADC output array

// Static variables
static int err;

/* Data of ADC io-channels specified in devicetree. */
const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), io_channels,
			     DT_SPEC_AND_COMMA)
};
uint32_t count = 0;
uint16_t buf;
struct adc_sequence sequence = {
        .buffer = &buf,
        /* buffer size in bytes, not number of samples */
        .buffer_size = sizeof(buf),
        .resolution = 10,
};

// forward declarations

void adc_timer_handler(struct k_work *dummy);

K_WORK_DEFINE(adc_work, adc_timer_handler);

void adc_register_work(struct k_timer *dummy){
        k_work_submit(&adc_work);
}
K_TIMER_DEFINE(adc_timer, adc_register_work, NULL);


int initMode(){
        k_poll_signal_init(&mode_switch_signal);
       /* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			//printk("ADC controller device %s not ready\n", adc_channels[i].dev->name);
			return -1;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);

		if (err < 0) {
			//printk("Could not setup channel #%d (%d)\n", i, err);
			return -1;
		}
	}
        return 0;
}

void readADC() //reads all ADC channels and stores mV outputs in adc_outputs_mv array
{
        //printk("ADC reading[%u]:\n", count++);
        for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {

                //printk("- %s, channel %d: ",
                // adc_channels[i].dev->name,
                // adc_channels[i].channel_id);
                

                (void)adc_sequence_init_dt(&adc_channels[i], &sequence);

                err = adc_read(adc_channels[i].dev, &sequence);
                if (err < 0) {
                        //printk("Could not read (%d)\n", err);
                        continue;
                }

                /*
                        * If using differential mode, the 16 bit value
                        * in the ADC sample buffer should be a signed 2's
                        * complement value.
                        */
                if (adc_channels[i].channel_cfg.differential) {
                        adc_outputs_mv[i] = (int32_t)((int16_t)buf);
                } else {
                        adc_outputs_mv[i] = (int32_t)buf;
                }
                //printk("Raw Value: %d, ", adc_outputs_mv[i]);
                //adc_outputs_mv[i] = adc_raw_to_millivolts_dt(&adc_channels[i], &val);
                adc_raw_to_millivolts(adc_ref_internal(adc_channels[i].dev), adc_channels[i].channel_cfg.gain, adc_channels[i].resolution, &adc_outputs_mv[i]);
                //adc_outputs_mv[i] = (int32_t)buf;
                //printk("MV Value: %d\n", adc_outputs_mv[i]);
        }
}


void adc_timer_handler(struct k_work *dummy){
    // This function is called periodically to simulate ADC readings
    // In a real application, you would read from an ADC here
    // For this example, we will just print a message
    printk("ADC Timer Handler: Simulating ADC reading\n");
    readADC(); // Simulated ADC value
    int32_t adc_value = adc_outputs_mv[SUPER_CAP_IDX]; 
    power_mode_t mode;
    if(adc_value < 1000){
        mode = POWER_LOW_MODE_NONE;
    } else if(adc_value < 2000){
        mode = POWER_MED_MODE_SYNC;
    } else{
        mode = POWER_HIGH_MODE_SYNC;
    }
    if(mode != current_power_mode){
        current_power_mode = mode;
        k_poll_signal_raise(&mode_switch_signal, 0);
        printk("Power mode changed to: %d\n", current_power_mode);
    }
}


