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
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/pm.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/time_units.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(Main,LOG_LEVEL_DBG);

/*Timing Constraints
ADC_TIME must be greater than the time it takes to read all ADC channels. readADC() is called ADC_TIME ms before robot_step() is called
MOTOR_SHUTOFF_TIME is the time the motors are left turned on after being triggered. This value must be smaller than ROBOT_STEP_TIME - ADC_TIME, 
        otherwise the ADC will be reading the motor capacitors while they are still being discharged
ROBOT_STEP_TIME is the time between robot steps. 
*/

//TIMING (all ms)
#define ADC_TIME 10 // Time delay to allow ADC reads of all channels
#define MOTOR_SHUTOFF_TIME 50 // Time delay to shutoff motors after being triggered
#define ROBOT_STEP_TIME 150 // Time delay between robot steps

// Define pins/state for capacitors
uint16_t cap_switch_state = 0; // 0: to supercap, 1: to motor cap
const struct device *gpio0;
const struct device *gpio1;
#define CAP_SWITCH_PIN 23  // gpio 0
#define MOTOR_1_CAP_PIN 13 // gpio 1
#define MOTOR_2_CAP_PIN 21 // gpio 0


int initCapGPIO()
{
        gpio0 = device_get_binding(DEVICE_DT_NAME(DT_NODELABEL(gpio0)));
        gpio1 = device_get_binding(DEVICE_DT_NAME(DT_NODELABEL(gpio1)));


        int cap_switch_pin_err;
        cap_switch_pin_err = gpio_pin_configure(gpio0, CAP_SWITCH_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        if(cap_switch_pin_err < 0) return -1;

#ifdef CONFIG_MILLIMOBILE_BOT
        int motor_1_cap_pin_err;
        int motor_2_cap_pin_err;
        motor_1_cap_pin_err = gpio_pin_configure(gpio1, MOTOR_1_CAP_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        motor_2_cap_pin_err = gpio_pin_configure(gpio0, MOTOR_2_CAP_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        if (motor_1_cap_pin_err < 0 || motor_2_cap_pin_err < 0) return -1;
#endif
       // printk("GPIO initialized\n");

        return 0;
}


#ifdef CONFIG_MILLIMOBILE_BOT

// Motion State (Left, Right, Straight)
#define STRAIGHT 0
#define LEFT 1
#define RIGHT 2
#define STOP 3
uint16_t motion_state = STRAIGHT;

void robot_step(struct k_work *work);

K_WORK_DEFINE(robot_step_work, robot_step);

void robot_step_timer_handler(struct k_timer *dummy)
{
        k_work_submit(&robot_step_work);
}
K_TIMER_DEFINE(robot_step_timer, robot_step_timer_handler, NULL);

void motorShutoff(struct k_work *work)
{
        gpio_pin_set(gpio1, MOTOR_1_CAP_PIN, 0);
        gpio_pin_set(gpio0, MOTOR_2_CAP_PIN, 0);
}

K_WORK_DEFINE(motor_shutoff_work, motorShutoff);

void motor_shutoff_timer_handler(struct k_timer *dummy)
{
        k_work_submit(&motor_shutoff_work);
}
K_TIMER_DEFINE(motor_shutoff_timer, motor_shutoff_timer_handler, NULL);


void robot_step(struct k_work *work)
{       
        if(adc_outputs_mv[SUPER_CAP_IDX] <= SUPER_CAP_THRESHOLD){
                gpio_pin_set(gpio0, CAP_SWITCH_PIN, 0);
                //printk("Switching to supercap\n");
        }
        else
        {
                gpio_pin_set(gpio0, CAP_SWITCH_PIN, 1);
                //printk("Switching to motor caps\n");
                if(adc_outputs_mv[MOTOR_CAP_0_IDX] >= MOTOR_CAP_RELEASE_THRESHOLD && adc_outputs_mv[MOTOR_CAP_1_IDX] >= MOTOR_CAP_RELEASE_THRESHOLD){
                        switch (motion_state)
                        {
                        case STRAIGHT:
                                gpio_pin_set(gpio1, MOTOR_1_CAP_PIN, 1);
                                gpio_pin_set(gpio0, MOTOR_2_CAP_PIN, 1);
                                break;
                        
                        case LEFT:
                                gpio_pin_set(gpio0, MOTOR_2_CAP_PIN, 1);                    
                                break;
                        case RIGHT:
                                gpio_pin_set(gpio1, MOTOR_1_CAP_PIN, 1);
                                break;
                        case STOP:
                                /* do nothing lol */
                                break;
                        default:
                                break;  
                        }
                       // printk("motors triggered\n");
                        
                        k_timer_start(&motor_shutoff_timer, K_MSEC(MOTOR_SHUTOFF_TIME), K_NO_WAIT);
                }
        }
        k_timer_start(&adc_timer, K_MSEC(ROBOT_STEP_TIME - ADC_TIME), K_NO_WAIT);
        k_timer_start(&robot_step_timer, K_MSEC(ROBOT_STEP_TIME), K_NO_WAIT);
        //printk("Robot Step\n");
        return;
}
#endif


#ifdef CONFIG_MILLIMOBILE_DEV_KIT

#define SW0_NODE DT_ALIAS(sw0)
#define SW1_NODE DT_ALIAS(sw1)
#define SW2_NODE DT_ALIAS(sw2)
#define SW3_NODE DT_ALIAS(sw3)

static const struct gpio_dt_spec buttons[] = {
    GPIO_DT_SPEC_GET(SW0_NODE, gpios),
    GPIO_DT_SPEC_GET(SW1_NODE, gpios),
    GPIO_DT_SPEC_GET(SW2_NODE, gpios),
    GPIO_DT_SPEC_GET(SW3_NODE, gpios),
};

static struct gpio_callback button_cbs[ARRAY_SIZE(buttons)];
/* Callback for button 0 */
void button0_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
        k_poll_signal_raise(&mode_switch_signal, 0);
        current_power_mode = POWER_LOW_MODE_NONE;
        printk("Button 0 pressed, new mode: %d\n", current_power_mode);
}

/* Callback for button 1 */
void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
        k_poll_signal_raise(&mode_switch_signal, 0);
        current_power_mode = POWER_MED_MODE_SYNC;
        printk("Button 0 pressed, new mode: %d\n", current_power_mode);
}


/* Callback for button 2 */
void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
        k_poll_signal_raise(&mode_switch_signal, 0);
        current_power_mode = POWER_HIGH_MODE_SYNC;
        printk("Button 0 pressed, new mode: %d\n", current_power_mode);
}

/* Callback for button 3 */
void button3_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins){
        k_poll_signal_raise(&mode_switch_signal, 0);
        current_power_mode = POWER_HIGH_MODE_ADV;
        printk("Button 0 pressed, new mode: %d\n", current_power_mode);
}

/* Array of function pointers for button callbacks */
void (*button_callbacks[])(const struct device *, struct gpio_callback *, uint32_t) = {
    button0_pressed,
    button1_pressed,
    button2_pressed,
    button3_pressed,
};

static int init_buttons(void)
{
    for (int i = 0; i < ARRAY_SIZE(buttons); i++) {
        const struct gpio_dt_spec *btn = &buttons[i];

        if (!device_is_ready(btn->port)) {
            printk("Error: button %d port %s not ready\n", i, btn->port->name);
            return -1;
        }

        /* configure as input with interrupt on active edge */
        gpio_pin_configure_dt(btn, GPIO_INPUT);

        gpio_init_callback(&button_cbs[i], button_callbacks[i], BIT(btn->pin));
        gpio_add_callback(btn->port, &button_cbs[i]);
        gpio_pin_interrupt_configure_dt(btn, GPIO_INT_EDGE_TO_ACTIVE);
    }
    return 0;
}


#endif



// Global state 
int main(void){
        printk("Millimobile main started\n");
        int ret;

        if (initCapGPIO() != 0) return -1;

	if (initMode() != 0) return -1;

#ifdef CONFIG_MILLIMOBILE_DEV_KIT
        if(init_buttons() != 0) return -1;
#endif
	k_timer_start(&adc_timer, K_MSEC(ADC_TIME), K_NO_WAIT);

#ifdef CONFIG_MILLIMOBILE_BOT
        k_timer_start(&robot_step_timer, K_MSEC(ADC_TIME*2), K_NO_WAIT);
#endif
        /* Initialize the Bluetooth Subsystem */
	ret = bt_enable(NULL);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return -1;
	}     
       
        current_power_mode = POWER_MED_MODE_SYNC; 
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
                k_sleep(K_MSEC(1000));
        }
}
