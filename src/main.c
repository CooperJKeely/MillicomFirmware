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
#include <zephyr/drivers/gpio.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Main, LOG_LEVEL_INF);

// --- Threading Resources ---
#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY 7

K_THREAD_STACK_DEFINE(sync_thread_stack_area, THREAD_STACK_SIZE);
struct k_thread sync_thread_data;
k_tid_t sync_tid = NULL;

K_THREAD_STACK_DEFINE(adv_thread_stack_area, THREAD_STACK_SIZE);
struct k_thread adv_thread_data;
k_tid_t adv_tid = NULL;

K_SEM_DEFINE(button_press_sem, 0, 1);

// --- Button Configuration ---
#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;

/*
 * The button ISR. Its only job is to signal the main thread.
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    // Disable the interrupt to prevent it from firing continuously
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_DISABLE);
    k_sem_give(&button_press_sem);
}

/* Helper function to re-enable the button interrupt */
static void button_interrupt_enable(void)
{
    int ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_LEVEL_ACTIVE);
    if (ret) {
        LOG_ERR("Failed to enable button interrupt (err %d)", ret);
    }
}


void main(void)
{
    int ret;

    // --- Standard Initialization ---
    if (!device_is_ready(button.port)) {
        LOG_ERR("Button device not ready");
        return;
    }
    ret = gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Failed to configure button pin (err %d)", ret);
        return;
    }
    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);
    k_poll_signal_init(&mode_switch_signal);

    LOG_INF("System initialized.");

    while (1) {
        LOG_INF("Entering low power state. Enabling button interrupt.");
        button_interrupt_enable();

        // Wait for a button press. This should be ~4uA.
        k_sem_take(&button_press_sem, K_FOREVER);
        LOG_INF("Button pressed, manager thread is active.");

        // --- Gracefully stop the running thread ---
        k_tid_t old_sync_tid = sync_tid;
        k_tid_t old_adv_tid = adv_tid;
        sync_tid = NULL;
        adv_tid = NULL;

        if (old_sync_tid != NULL || old_adv_tid != NULL) {
            k_poll_signal_raise(&mode_switch_signal, 0);
            if (old_sync_tid != NULL) { k_thread_join(old_sync_tid, K_MSEC(500)); }
            if (old_adv_tid != NULL) { k_thread_join(old_adv_tid, K_MSEC(500)); }

            if(bt_disable()) {
                LOG_ERR("Bluetooth disable failed");
            }
        }

        // Cycle power modes
        if (current_power_mode == POWER_LOW_MODE_NONE) {
            current_power_mode = POWER_MED_MODE_SYNC;
        } else if (current_power_mode == POWER_MED_MODE_SYNC) {
            current_power_mode = POWER_HIGH_MODE_ADV;
        } else {
            current_power_mode = POWER_LOW_MODE_NONE;
        }
        LOG_INF("Switching power mode to %d", current_power_mode);

        // Enable Bluetooth ONLY if we are entering an active mode
        if (current_power_mode != POWER_LOW_MODE_NONE) {
            if (bt_enable(NULL)) {
                LOG_ERR("Bluetooth re-enable failed");
                continue;
            }

            // Start the new thread
            if (current_power_mode == POWER_MED_MODE_SYNC) {
                LOG_INF("Starting sync thread...");
                sync_tid = k_thread_create(&sync_thread_data, sync_thread_stack_area,
                                         K_THREAD_STACK_SIZEOF(sync_thread_stack_area),
                                         (k_thread_entry_t)sync_thread, NULL, NULL, NULL,
                                         THREAD_PRIORITY, 0, K_NO_WAIT);
            } else if (current_power_mode == POWER_HIGH_MODE_ADV) {
                LOG_INF("Starting adv thread...");
                adv_tid = k_thread_create(&adv_thread_data, adv_thread_stack_area,
                                        K_THREAD_STACK_SIZEOF(adv_thread_stack_area),
                                        (k_thread_entry_t)adv_thread, NULL, NULL, NULL,
                                        THREAD_PRIORITY, 0, K_NO_WAIT);
            }
        }

        /*
         * FIX: Add a debounce delay.
         * This prevents the loop from immediately re-enabling the interrupt
         * while the button is still physically pressed.
         */
        k_msleep(500); // 500ms debounce delay
    }
}
