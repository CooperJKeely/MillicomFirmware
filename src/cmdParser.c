#include "cmdParser.h"
#include <zephyr/bluetooth/hci_vs.h>

#if defined(CONFIG_MILLIMOBILE_CMD)
// command variable from main
extern uint8_t command;
#endif

uint8_t parse_command(void){
    //LOG_ERR("Beginning Comand Parsing");
    switch(command) {
        case 0:
            //On-board Temperature
            return get_temperature();
        case 1:
            // Standby
            return -1;
        case 2:
            // Capacitor
            return 0;   // Placeholder value
        case 3:
            //I2C Temperature
            return i2c_get_temperature();
        default:
            // Default case - should never reach
            return 0;   // Placeholder value
    } 
};

int8_t get_temperature(void){
    // This function reads the temperature from the onboard temperature sensor
    // returns the current temperature value in celsius formatted in hex
    int err = 0;
    struct net_buf *buf, *rsp = NULL;
    struct bt_hci_rp_vs_read_chip_temp *cmd_params;
    struct bt_hci_rp_vs_read_chip_temp *rsp_params;

    buf = bt_hci_cmd_create(BT_HCI_OP_VS_READ_CHIP_TEMP, sizeof(*cmd_params));
    if (!buf) {
        printk("Could not allocate command buffer");
        return -ENOMEM;
    }

    cmd_params = net_buf_add(buf, sizeof(*cmd_params));

    err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_READ_CHIP_TEMP, buf, &rsp);
    if (err) {
        printk("bt_hci_cmd_send_sync failed (err: %d)",err);
        return err;
    }

    rsp_params = (void *) rsp->data;
    net_buf_unref(rsp);

    printk("Current Temp: %d \n", rsp_params->temps);

    return rsp_params->temps;
    /*
    // include/bluetooth/hci_vs.h
    #define BT_HCI_OP_VS_READ_CHIP_TEMP             BT_OP(BT_OGF_VS, 0x000b)
    struct bt_hci_rp_vs_read_chip_temp {
	    u8_t  status;
	    s8_t  temps;
    } __packed;
    */
}