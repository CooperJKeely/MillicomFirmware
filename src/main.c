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
#include <zephyr/drivers/adc.h>
#include <zephyr/pm/pm.h>
#include <zephyr/device.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/time_units.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(Main,LOG_LEVEL_DBG);

//#include <pm/pm.h>  
// #include <device.h> 
void robot_step(struct k_work *work);

//TIMING (all ms)
#define ADC_TIME 10 // Time delay to allow ADC reads of all channels
#define MOTOR_SHUTOFF_TIME 50 // Time delay to shutoff motors after being triggered
#define ROBOT_STEP_TIME 150 // Time delay between robot steps

/*Timing Constraints
ADC_TIME must be greater than the time it takes to read all ADC channels. readADC() is called ADC_TIME ms before robot_step() is called
MOTOR_SHUTOFF_TIME is the time the motors are left turned on after being triggered. This value must be smaller than ROBOT_STEP_TIME - ADC_TIME, 
        otherwise the ADC will be reading the motor capacitors while they are still being discharged
ROBOT_STEP_TIME is the time between robot steps. 
*/


//////////////////////// ADC //////////////////////////


// Constants for min val read on ADC for capacitor to discharge/recharge
#define SUPER_CAP_THRESHOLD_MV 2000 //190// Need to tune this value
#define MOTOR_CAP_RELEASE_THRESHOLD_MV 3000//310 //need to tune
#define SUPER_CAP_THRESHOLD (SUPER_CAP_THRESHOLD_MV / 2)
#define MOTOR_CAP_RELEASE_THRESHOLD (MOTOR_CAP_RELEASE_THRESHOLD_MV / 6)

// Define pins/state for capacitors
uint16_t cap_switch_state = 0; // 0: to supercap, 1: to motor cap
const struct device *gpio0;
const struct device *gpio1;
#define CAP_SWITCH_PIN 23  // gpio 0
#define MOTOR_1_CAP_PIN 13 // gpio 1
#define MOTOR_2_CAP_PIN 21 // gpio 0

// error variable
int err;

// ADC defs
#if !DT_NODE_EXISTS(DT_PATH(zephyr_user)) || \
	!DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#error "No suitable devicetree overlay specified"
#endif

#define DT_SPEC_AND_COMMA(node_id, prop, idx) \
	ADC_DT_SPEC_GET_BY_IDX(node_id, idx),

/* Data of ADC io-channels specified in devicetree. */
static const struct adc_dt_spec adc_channels[] = {
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

// ADC MV Buffer
int32_t adc_outputs_mv[ARRAY_SIZE(adc_channels)];
#define SUPER_CAP_IDX 0
#define MOTOR_CAP_1_IDX 1
#define MOTOR_CAP_2_IDX 2

// Motion State (Left, Right, Straight)
#define STRAIGHT 0
#define LEFT 1
#define RIGHT 2
#define STOP 3
uint16_t motion_state = STRAIGHT;

K_WORK_DEFINE(robot_step_work, robot_step);

void robot_step_timer_handler(struct k_timer *dummy)
{
        k_work_submit(&robot_step_work);
}
K_TIMER_DEFINE(robot_step_timer, robot_step_timer_handler, NULL);


int initCapGPIO()
{
        gpio0 = device_get_binding(DEVICE_DT_NAME(DT_NODELABEL(gpio0)));
        gpio1 = device_get_binding(DEVICE_DT_NAME(DT_NODELABEL(gpio1)));
        int cap_switch_pin_err;
        int motor_1_cap_pin_err;
        int motor_2_cap_pin_err;

        cap_switch_pin_err = gpio_pin_configure(gpio0, CAP_SWITCH_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        motor_1_cap_pin_err = gpio_pin_configure(gpio1, MOTOR_1_CAP_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        motor_2_cap_pin_err = gpio_pin_configure(gpio0, MOTOR_2_CAP_PIN, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        if (cap_switch_pin_err < 0 || motor_1_cap_pin_err < 0 || motor_2_cap_pin_err < 0) return -1;

       // printk("GPIO initialized\n");

        return 0;
}

int initADC()
{
       /* Configure channels individually prior to sampling. */
	for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_channels[i].dev)) {
			//printk("ADC controller device %s not ready\n", adc_channels[i].dev->name);
			return 1;
		}

		err = adc_channel_setup_dt(&adc_channels[i]);

		if (err < 0) {
			//printk("Could not setup channel #%d (%d)\n", i, err);
			return 1;
		}
	}
        return 0;
}

void readADC(struct k_work *work) //reads all ADC channels and stores mV outputs in adc_outputs_mv array
{
       //printk("ADC reading[%u]:\n", count++);
        for (size_t i = 0U; i < ARRAY_SIZE(adc_channels); i++) {

                //printk("- %s, channel %d: ",
                //        adc_channels[i].dev->name,
                //        adc_channels[i].channel_id);
                

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
               // printk("Raw Value: %d, ", adc_outputs_mv[i]);
                //adc_outputs_mv[i] = adc_raw_to_millivolts_dt(&adc_channels[i], &val);
                adc_raw_to_millivolts(adc_ref_internal(adc_channels[i].dev), adc_channels[i].channel_cfg.gain, adc_channels[i].resolution, &adc_outputs_mv[i]);
                //adc_outputs_mv[i] = (int32_t)buf;
               // printk("MV Value: %d\n", adc_outputs_mv[i]);
        }
}
K_WORK_DEFINE(adc_work, readADC);

void adc_timer_handler(struct k_timer *dummy)
{
        k_work_submit(&adc_work);
}
K_TIMER_DEFINE(adc_timer, adc_timer_handler, NULL);

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
                if(adc_outputs_mv[MOTOR_CAP_1_IDX] >= MOTOR_CAP_RELEASE_THRESHOLD && adc_outputs_mv[MOTOR_CAP_2_IDX] >= MOTOR_CAP_RELEASE_THRESHOLD){
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


// Global state 
int main(void){
        printk("Millimobile main started\n");
        int ret;
        if (initCapGPIO() != 0) return -1;

	if (initADC() != 0) return -1;

	k_timer_start(&adc_timer, K_MSEC(ADC_TIME), K_NO_WAIT);
        k_timer_start(&robot_step_timer, K_MSEC(ADC_TIME*2), K_NO_WAIT);

        // initialize the kernel signal
        k_poll_signal_init(&mode_switch_signal);
        
        /* Initialize the Bluetooth Subsystem */
	ret = bt_enable(NULL);
	if (ret) {
		LOG_ERR("Bluetooth init failed (err %d)", ret);
		return -1;
	}     
       
        LOG_INF("Start adc simulation");
        start_adc_simulation();
        
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
