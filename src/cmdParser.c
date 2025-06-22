#include "cmdParser.h"
#include <zephyr/bluetooth/hci_vs.h>

int parse_command(void){
    switch(command) {
        case 0:
            // Temperature
            return get_temperature();
        case 1:
            // Standby
            return -1;
        case 2:
            // Capacitor
            return 0;   // Placeholder value
        default:
            // Default case - should never reach
            return 0;   // Placeholder value
    } 
};

int get_temperature(void){
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