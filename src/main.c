#ifdef CONFIG_MILLIMOBILE_ADV
#include "adv.h"
#endif
#ifdef CONFIG_MILLIMOBILE_SYNC
#include "sync.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

// enable logging
//#include <zephyr/logging/log.h>
//LOG_MODULE_REGISTER(Main,LOG_LEVEL_DBG);


// thread code
//K_MUTEX_DEFINE(bluetooth_execution);


// button code
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
        printk("Button Pressed\n");
}

static struct gpio_callback button_cb_data;

int main(void){
        int ret;
        if(!device_is_ready(button.port)){
                return -1;
        }
        
        ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
        if(ret < 0){
                return -1;                
        }

        ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);

        gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));

        gpio_add_callback(button.port, &button_cb_data);
        
        adv_thread();
        printk("Sync Thread Exited");
        while(1){
                k_msleep(1000);
        }
}
