#ifndef ADV_H
#define ADV_H

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include "mode.h"

#define NUM_RSP_SLOTS 1
#define NUM_SUBEVENTS 2
#define PACKET_SIZE   5
#define NAME_LEN      30


#define MAX_SYNCS (NUM_SUBEVENTS * NUM_RSP_SLOTS)

struct pawr_timing {
	uint8_t subevent;
	uint8_t response_slot;
} __packed;

void disconnected_cb(struct bt_conn *conn, uint8_t reason);
void remote_info_available_cb(struct bt_conn *conn, struct bt_conn_remote_info *remote_info);
void init_bufs(void);
void connected_cb(struct bt_conn *conn, uint8_t err);
void adv_thread(void);
#endif