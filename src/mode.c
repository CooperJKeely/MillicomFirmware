#include "mode.h"
#include <stdlib.h>

volatile power_mode_t current_power_mode = POWER_LOW_MODE_NONE; 
struct k_poll_signal mode_switch_signal;

static struct k_timer adc_timer;

static int32_t read_cap_voltage(void){
    // This function simulates reading a capacitor voltage
    // In a real application, you would read from an ADC here
    // For this example, we will just return a random value
    return (int32_t)((rand() % 4000)); // Simulated ADC value between 1000 and 4000
}

void adc_timer_handler(struct k_timer *timer_id){
    // This function is called periodically to simulate ADC readings
    // In a real application, you would read from an ADC here
    // For this example, we will just print a message
    printk("ADC Timer Handler: Simulating ADC reading\n");
    int32_t adc_value = read_cap_voltage(); // Simulated ADC value
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

void start_adc_simulation(void){
    // This function starts the ADC simulation
    // In a real application, you would configure the ADC here
    // For this example, we will just start a timer to simulate ADC readings
    k_timer_init(&adc_timer, adc_timer_handler, NULL);
    k_timer_start(&adc_timer, K_NO_WAIT, K_SECONDS(20));
}

power_mode_t get_power(void){
    return current_power_mode;
}

