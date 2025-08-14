#include "sync.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(Sync,LOG_LEVEL_DBG);

static bool is_legacy_advertising = false; 
static bool is_shutting_down = false;

static K_SEM_DEFINE(sem_per_adv, 0, 1);
static K_SEM_DEFINE(sem_per_sync, 0, 1);
static K_SEM_DEFINE(sem_per_sync_lost, 0, 1);
static K_SEM_DEFINE(sem_handshake_done, 0, 1); 

static struct k_poll_event sync_events[] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					&sem_per_sync, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					&sem_per_sync_lost, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					&mode_switch_signal, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					&sem_handshake_done, 0),
};

static struct bt_conn *default_conn;
static struct bt_le_per_adv_sync *default_sync;
static struct __packed {
	uint8_t subevent;
	uint8_t response_slot;
} pawr_timing;


static void sync_cb(struct bt_le_per_adv_sync *sync, struct bt_le_per_adv_sync_synced_info *info)
{
	struct bt_le_per_adv_sync_subevent_params params;
	uint8_t subevents[1];
	char le_addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
	printk("Synced to %s with %d subevents\n", le_addr, info->num_subevents);

	default_sync = sync;

	params.properties = 0;
	params.num_subevents = 1;
	params.subevents = subevents;
	subevents[0] = pawr_timing.subevent;

	err = bt_le_per_adv_sync_subevent(sync, &params);
	if (err) {
		printk("Failed to set subevents to sync to (err %d)\n", err);
	} else {
		printk("Changed sync to subevent %d\n", subevents[0]);
	}

	k_sem_give(&sem_per_sync);
}

static void term_cb(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_term_info *info)
{
	char le_addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

	printk("Sync terminated (reason %d)\n", info->reason);

	default_sync = NULL;

	k_sem_give(&sem_per_sync_lost);
}

static bool print_ad_field(struct bt_data *data, void *user_data)
{
	ARG_UNUSED(user_data);

	printk("    0x%02X: ", data->type);
	for (size_t i = 0; i < data->data_len; i++) {
		printk("%02X", data->data[i]);
	}

	printk("\n");

	return true;
}

int bt_le_per_adv_set_response_data(struct bt_le_per_adv_sync *per_adv_sync,
				    const struct bt_le_per_adv_response_params *params,
				    const struct net_buf_simple *data);

static struct bt_le_per_adv_response_params rsp_params;

NET_BUF_SIMPLE_DEFINE_STATIC(rsp_buf, 247);

static void recv_cb(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_recv_info *info, struct net_buf_simple *buf)
{
	int err;

	if (buf && buf->len) {
		/* Echo the data back to the advertiser
		   Reset the response buffer, and assign received buffer data to it
		*/
		net_buf_simple_reset(&rsp_buf);
		/* 	Entry point for changing response data based on command 
			Copy given number of bytes from memory to the end of the buffer.

			Increments the data length of the buffer to account for more data at the end.

			Parameters
    			buf	Buffer to update.
    			mem	Location of data to be added.
    			len	Length of data to be added

			Returns
    			The original tail of the buffer. 
		
			Reference Documentation:
			https://docs.zephyrproject.org/apidoc/latest/group__net__buf.html#gac37209c1e5097e5610860943fb7d0115
		*/
		// Original: net_buf_simple_add_mem(&rsp_buf, buf->data, buf->len);
	
		#if defined(CONFIG_MILLIMOBILE_CMD)
			// Copy buffer data (command) into relevant variable
			// Parse command & store result in buffer
			uint8_t result = parse_command(0);
			//uint8_t result = 25;
			buf->data[buf->len - 1] = result;
			printk("Sending Data: %d\n", result);
			// Configure message for sending
			net_buf_simple_add_mem(&rsp_buf, buf->data, buf->len);
		#else
			net_buf_simple_add_mem(&rsp_buf, buf->data, buf->len);
		#endif

		rsp_params.request_event = info->periodic_event_counter;
		rsp_params.request_subevent = info->subevent;
		/* Respond in current subevent and assigned response slot */
		rsp_params.response_subevent = info->subevent;
		rsp_params.response_slot = pawr_timing.response_slot;

		/* Print response data to terminal */
		printk("Indication: subevent %d, responding in slot %d\n", info->subevent,
		       pawr_timing.response_slot);
		bt_data_parse(buf, print_ad_field, NULL);

		/* 	Data to be sent back to the advertiser
			Set the data for a response slot in a specific subevent of the PAwR.

			This function is called by the application to set the response data.
			The data for a response slot shall be transmitted only once.

			Parameters
    			per_adv_sync	The periodic advertising sync object.
    			params	Parameters.
    			data	The response data to send

		   	Reference Documentation:
		   	https://docs.zephyrproject.org/apidoc/latest/group__bt__gap.html#gaae6b8583f7d5457f20b03dccd146425e
		*/
		err = bt_le_per_adv_set_response_data(sync, &rsp_params, &rsp_buf);
		if (err) {
			printk("Failed to send response (err %d)\n", err);
		}
	} else if (buf) {
		printk("Received empty indication: subevent %d\n", info->subevent);
	} else {
		printk("Failed to receive indication: subevent %d\n", info->subevent);
	}
}

static struct bt_le_per_adv_sync_cb sync_callbacks = {
	.synced = sync_cb,
	.term = term_cb,
	.recv = recv_cb,
};

static const struct bt_uuid_128 pawr_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0));
static const struct bt_uuid_128 pawr_char_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));

static ssize_t write_timing(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			    uint16_t len, uint16_t offset, uint8_t flags)
{
	if (offset) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len != sizeof(pawr_timing)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(&pawr_timing, buf, len);

	printk("New timing: subevent %d, response slot %d\n", pawr_timing.subevent,
	       pawr_timing.response_slot);

	struct bt_le_per_adv_sync_subevent_params params;
	uint8_t subevents[1];
	int err;

	params.properties = 0;
	params.num_subevents = 1;
	params.subevents = subevents;
	subevents[0] = pawr_timing.subevent;

	if (default_sync) {
		err = bt_le_per_adv_sync_subevent(default_sync, &params);
		if (err) {
			printk("Failed to set subevents to sync to (err %d)\n", err);
		} else {
			printk("Changed sync to subevent %d\n", subevents[0]);
		}
	} else {
		printk("Not synced yet\n");
	}

	return len;
}

BT_GATT_SERVICE_DEFINE(pawr_svc, BT_GATT_PRIMARY_SERVICE(&pawr_svc_uuid.uuid),
		       BT_GATT_CHARACTERISTIC(&pawr_char_uuid.uuid, BT_GATT_CHRC_WRITE,
					      BT_GATT_PERM_WRITE, NULL, write_timing,
					      &pawr_timing));

void connected(struct bt_conn *conn, uint8_t err)
{
	if (is_shutting_down) {
		LOG_WRN("Connection callback ignored: thread is shutting down.");
		return;
	}

	LOG_INF("--> Log: `connected` callback entered."); 

	printk("Connected, err 0x%02X %s\n", err, bt_hci_err_to_str(err));

	if (err) {
		default_conn = NULL;

		return;
	}

	is_legacy_advertising = false;


	default_conn = bt_conn_ref(conn);

    k_sem_give(&sem_handshake_done);
}

void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("--> Log: `disconnected` callback entered."); 

	if(is_shutting_down) {
        LOG_WRN("Disconnected callback ignored: thread is shutting down.");
		return;
	}

	if (default_conn == conn) {
		k_sem_give(&sem_handshake_done); 
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

	printk("Disconnected, reason 0x%02X %s\n", reason, bt_hci_err_to_str(reason));
}

/*
BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected,
	.disconnected = disconnected,
};
*/

static struct bt_conn_cb sync_conn_cb = {
	.connected = connected,
	.disconnected = disconnected,
};

static const struct bt_data ad[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

void sync_thread(void)
{
	struct bt_le_per_adv_sync_transfer_param past_param;
	int err;

	// Register bt connection callbacks
    bt_conn_cb_register(&sync_conn_cb);

	is_shutting_down = false;

	LOG_INF("`sync_thread` has started."); 

	bt_le_per_adv_sync_cb_register(&sync_callbacks);

	past_param.skip = 1;
	past_param.timeout = 1000; /* 10 seconds */
	past_param.options = BT_LE_PER_ADV_SYNC_TRANSFER_OPT_NONE;
	err = bt_le_per_adv_sync_transfer_subscribe(NULL, &past_param);
	if (err) {
		LOG_ERR("PAST subscribe failed (err %d)", err);
		return ;
	}

	while(1){
		uint8_t timeout_counter = 0;
		err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
		if (err && err != -EALREADY) {
			LOG_ERR("Advertising failed to start (err %d)", err);
			return ;
		}
		if (err == 0) { 
       		is_legacy_advertising = true; 
   		}	

		LOG_INF("Waiting for periodic sync...");
		while(1){
			err = k_poll(sync_events, ARRAY_SIZE(sync_events), K_SECONDS(5));
			if(err == 0){
				if(sync_events[0].sem->count > 0){
					k_sem_take(&sem_per_sync, K_NO_WAIT);	
					LOG_INF("Periodic sync established, receiving data.");
					goto sync_established;
				} else if (sync_events[3].sem->count > 0) { // BLE Handshake Started
					k_sem_take(&sem_handshake_done, K_NO_WAIT);
					LOG_INF("Temporary BLE connection established. Waiting for disconnect after PAST.");

					// Wait for the disconnect signal (sem_handshake_done again) or a power mode change.
					err = k_poll(&sync_events[2], 2, K_SECONDS(10)); // Listen starting at mode_switch_signal for 2 events

					if (err == -EAGAIN) {
						// The advertiser failed to disconnect us.
						LOG_WRN("Timed out waiting for post-PAST disconnect. Forcing cleanup.");
						goto mode_switch;
					}
					
					// Disconnect happened normally OR mode changed.
					k_sem_take(&sem_handshake_done, K_NO_WAIT); // Clear the semaphore if it was given
					LOG_INF("Post-PAST disconnect received or mode changed. Restarting scan.");
					continue; // Loop back to the main "waiting for sync" state.
				} else if(sync_events[2].signal->signaled){
					k_poll_signal_reset(&mode_switch_signal);
					LOG_INF("Power mode change requested, exiting sync thread.");
					goto mode_switch;
				}
			} else if (err == -EAGAIN) {
				LOG_WRN("Polling timed out, retrying...");
				timeout_counter++;
				if (timeout_counter >= timeout_threshold && current_power_mode == POWER_HIGH_MODE_SYNC) {
					current_power_mode = POWER_HIGH_MODE_ADV;	
					LOG_INF("Power mode changed to HIGH_MODE_ADV due to timeout.");	
					
					// k_poll_signal_raise(&mode_switch_signal, 0); // <-- DELETE THIS LINE
					
					goto mode_switch;
				}
				continue;
			} else {
				LOG_ERR("Polling failed (err %d)", err);
				return ;
			}
		}
		continue;
sync_established:
		LOG_INF("Periodic sync established, waiting for data...");
		err = k_poll(&sync_events[1], 2, K_FOREVER);
		if (err == 0){
			if (sync_events[1].sem->count > 0){
				k_sem_take(&sem_per_sync_lost, K_NO_WAIT);
				LOG_INF("Periodic sync lost, re-establishing.");
				//continue;
				goto mode_switch;
			} else if (sync_events[2].signal->signaled) {
				k_poll_signal_reset(&mode_switch_signal);
				LOG_INF("Power mode change requested, exiting sync thread.");
				goto mode_switch;
			}
		}
	
	}
mode_switch:
	is_shutting_down = true;
    LOG_INF("Entered `mode_switch` cleanup block."); 

	bt_le_scan_stop();

    // Unregister the sync callbacks by registering NULL. Gemini suggestion 
	// but I can't find what this function does to verify if this is correct
    //bt_le_per_adv_sync_cb_register(NULL);

	// Unsubscribe from PAST. The NULL parameter handles
    // the general subscription created at the start of the thread.
    err = bt_le_per_adv_sync_transfer_unsubscribe(NULL);
    if (err) {
        // This may return an error if there was no active subscription,
        // which is okay. We log it but don't treat it as a fatal error.
        LOG_WRN("bt_le_per_adv_sync_transfer_unsubscribe failed (err %d)", err);
    }

	if (default_sync) {
		bt_le_per_adv_sync_delete(default_sync);
		default_sync = NULL;
	}

	if (default_conn) {
		LOG_INF("`default_conn` is valid, calling unsubscribe."); 

		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}

    if (is_legacy_advertising) {
        int stop_err = bt_le_adv_stop();
        if (stop_err) {
            LOG_ERR("bt_le_adv_stop failed (err %d)", stop_err);
        }
        is_legacy_advertising = false;
    }	

	// Unregister bt connection callbacks
	bt_conn_cb_unregister(&sync_conn_cb);
	LOG_INF("Exiting sync thread due to power mode change.");
	return ;
}
