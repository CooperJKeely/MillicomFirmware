#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C0_NODE DT_NODELABEL(hdc3022)
#define I2C_SPEED_STANDARD   (0x1U)

uint8_t data;
static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);

int8_t device_status(void){

    if (!device_is_ready(dev_i2c.bus)) {
        printk("I2C bus %s is not ready!\n\r",dev_i2c.bus->name);
        return -1;
    }
}

void get_temperature(void){

    ret = i2c_read_dt(&dev_i2c, &data, sizeof(data));
    if(ret != 0){
        printk("Failed to read from I2C device address %x at Reg. %x \n\r", dev_i2c.addr,config[0]);
    }
}
