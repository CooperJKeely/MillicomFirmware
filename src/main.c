#ifdef CONFIG_MILLIMOBILE_ADV
#include "adv.h"
#endif
#ifdef CONFIG_MILLIMOBILE_SYNC
#include "sync.h"
#endif
#ifdef CONFIG_MILLIMOBILE_MODE_SWITCHING
#include "mode.h"
#endif
#ifdef CONFIG_MILLIMOBILE_CMD
#include "cmdParser.h"
uint8_t command = 0; // Default to temp sensor mode
#endif

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(Main,LOG_LEVEL_DBG);

// button code
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
        k_poll_signal_raise(&mode_switch_signal, 0);
        printk("Button Pressed: switching power mode\n");
        if(current_power_mode == POWER_LOW_MODE_NONE){
                current_power_mode = POWER_MED_MODE_SYNC;
        } else if (current_power_mode == POWER_MED_MODE_SYNC){
                current_power_mode = POWER_HIGH_MODE_ADV;
        } else if (current_power_mode == POWER_HIGH_MODE_ADV){
                current_power_mode = POWER_LOW_MODE_NONE;
        } else {
                current_power_mode = POWER_LOW_MODE_NONE;
        }
        printk("Current power mode: %d\n", current_power_mode);
}

static struct gpio_callback button_cb_data;

// Global state 
int main(void){
        int ret;
        if(!device_is_ready(button.port)){
                LOG_ERR("Button device not ready");
                return -1;
        }
        
        ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
        if(ret < 0){
                LOG_ERR("Failed to configure button pin (err %d)", ret);
                return -1;                
        }

        ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_LEVEL_ACTIVE);

        gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));

        gpio_add_callback(button.port, &button_cb_data);

        // initialize the kernel signal
        k_poll_signal_init(&mode_switch_signal);
        
        /* Initialize the Bluetooth Subsystem */
	ret = bt_enable(NULL);
	if (ret ) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return -1;
	}     
       
        LOG_INF("Start adc simulation");
        //start_adc_simulation();
        
        LOG_INF("Entering main loop, current power mode: %d", current_power_mode);
        while(1){
                if(current_power_mode == POWER_LOW_MODE_NONE){
                        LOG_INF("Power mode is low, continue");
                }else if (current_power_mode == POWER_MED_MODE_SYNC || current_power_mode == POWER_HIGH_MODE_SYNC){
                        LOG_INF("Power mode is medium, starting sync thread");
                        sync_thread();
                }else if (current_power_mode == POWER_HIGH_MODE_ADV){
                        LOG_INF("Power mode is high, starting adv thread");
                        adv_thread();
                }
                k_msleep(1000);
        }
}
