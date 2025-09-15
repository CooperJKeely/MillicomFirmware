#include "sensor.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C1_NODE DT_NODELABEL(hdc3022)
#define I2C_SPEED_STANDARD (0x1U)

static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C1_NODE); //Defines I2C device

uint8_t device_status(void){
    //Checks if device is connected
    if (!device_is_ready(dev_i2c.bus)) {
        printk("I2C bus %s is not ready!\n\r",dev_i2c.bus->name);
        return 1;
    }
    return 0;
};

int8_t i2c_get_temperature(void){
    //Reads temperature and prints data
    if(device_status()){
        return 0;
    }

    uint8_t cmd[2] = {0x24, 0x00};//Writes command to I2C device register
    uint8_t rx_buf[6] = {0};//Holds sensor

    int ret = i2c_write_read_dt(&dev_i2c, cmd, sizeof(cmd), rx_buf, sizeof(rx_buf));

    if (ret < 0) {
        printk("i2c_write_read_dt failed: %d", ret);
        return 1;
    }

    uint16_t raw_temp = ((uint16_t)rx_buf[0] << 8) | rx_buf[1];//Raw data
    double temperature_c = ((float)raw_temp / 65536.0f) * 165.0f - 40.0f;//Converted to celsius
    double temperature_f = temperature_c * 1.8 + 32;//Converted to fahrenheit

    //printk("Temperature (Celsius): %.2f C\n", temperature_c);
    printk("Temperature (Fahrenheit): %.2f F\n", temperature_f);

    return temperature_f;
};

