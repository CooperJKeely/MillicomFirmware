#include "adv.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(Advertizer,LOG_LEVEL_DBG);

static bool is_shutting_down = false;

static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_discovered, 0, 1);
static K_SEM_DEFINE(sem_written, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);

static struct k_poll_event events[] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					&sem_connected, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY,
					&sem_disconnected, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					&mode_switch_signal, 0),
};

static struct bt_uuid_128 pawr_char_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));
static uint16_t pawr_attr_handle;
static const struct bt_le_per_adv_param per_adv_params = {
	.interval_min = 0x320,
	.interval_max = 0x320,
	.options = 0,
	.num_subevents = NUM_SUBEVENTS,
	.subevent_interval = 0x10,
	.response_slot_delay = 0x5,
	.response_slot_spacing = 0x5,
	.num_response_slots = NUM_RSP_SLOTS,
};

static struct bt_le_per_adv_subevent_data_params subevent_data_params[NUM_SUBEVENTS];
static struct net_buf_simple bufs[NUM_SUBEVENTS];
static uint8_t backing_store[NUM_SUBEVENTS][PACKET_SIZE];

BUILD_ASSERT(ARRAY_SIZE(bufs) == ARRAY_SIZE(subevent_data_params));
BUILD_ASSERT(ARRAY_SIZE(backing_store) == ARRAY_SIZE(subevent_data_params));

typedef struct{
	uint32_t heartbeat;
	bool free;
} subevent_state_t;
static subevent_state_t subevent_states[NUM_SUBEVENTS] = {0};
static int heartbeat_threshold = 5;
static uint8_t num_synced;

static inline bool check_for_lost_connections(){
	bool lost = false;
	for(int i = 0; i < NUM_SUBEVENTS; i ++){
		if(subevent_states[i].heartbeat >= heartbeat_threshold && !subevent_states[i].free){
			num_synced --;
			subevent_states[i].free = true;
			subevent_states[i].heartbeat = 0;
			lost = true;
		}
	}
	return lost;
}


static int counter = 0;

#if defined(CONFIG_MILLIMOBILE_CMD)
// command variable from main
extern uint8_t command;
#endif

static void request_cb(struct bt_le_ext_adv *adv, const struct bt_le_per_adv_data_request *request){
	int err;
	uint8_t to_send;
	struct net_buf_simple *buf;

	to_send = MIN(request->count, ARRAY_SIZE(subevent_data_params));

	for (size_t i = 0; i < to_send; i++) {
		buf = &bufs[i];
		/* Data being sent to the sync device, and then sent back*/
		// Original: buf->data[buf->len - 1] = counter++;
		buf->data[buf->len - 1] = command;
		size_t idx = (request->start + i) % per_adv_params.num_subevents;
		subevent_data_params[i].subevent = idx;
		subevent_data_params[i].response_slot_start = 0;
		subevent_data_params[i].response_slot_count = NUM_RSP_SLOTS;
		subevent_data_params[i].data = buf;

		if(!subevent_states[idx].free){
			subevent_states[idx].heartbeat++;
		}

	}


	err = bt_le_per_adv_set_subevent_data(adv, to_send, subevent_data_params);
	if (err) {
		printk("Failed to set subevent data (err %d)\n", err);
	} else {
		printk("Subevent data set %d\n", counter);
	}
}

static bool print_ad_field(struct bt_data *data, void *user_data){
	ARG_UNUSED(user_data);

	printk("    0x%02X: ", data->type);
	for (size_t i = 0; i < data->data_len; i++) {
		printk("%02X", data->data[i]);
	}
	printk("\n");

	return true;
}

static struct bt_conn *default_conn;

static void response_cb(struct bt_le_ext_adv *adv, struct bt_le_per_adv_response_info *info,
		     struct net_buf_simple *buf){
	if (buf) {
		if(!subevent_states[info->response_slot].free){
			subevent_states[info->response_slot].heartbeat--;
		}
		printk("Response: subevent %d, slot %d\n", info->subevent, info->response_slot);
		// Begin Debug
		uint8_t result = buf->data[buf->len - 1];
		printk("Received data: %d", result);
		// End Debug
		bt_data_parse(buf, print_ad_field, NULL);
	}
}

static const struct bt_le_ext_adv_cb adv_cb = {
	.pawr_data_request = request_cb,
	.pawr_response = response_cb,
};

void connected_cb(struct bt_conn *conn, uint8_t err){
	if (is_shutting_down) {
		LOG_WRN("Connection callback ignored: adv_thread is shutting down.");
		return;
	}

	printk("Connected (err 0x%02X)\n", err);

	__ASSERT(conn == default_conn, "Unexpected connected callback");

	if (err) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
}

void disconnected_cb(struct bt_conn *conn, uint8_t reason){
	printk("Disconnected, reason 0x%02X %s\n", reason, bt_hci_err_to_str(reason));
	k_sem_give(&sem_disconnected);
}

void remote_info_available_cb(struct bt_conn *conn, struct bt_conn_remote_info *remote_info){
	/* Need to wait for remote info before initiating PAST */
	k_sem_give(&sem_connected);
}

/*
BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.remote_info_available = remote_info_available_cb,
};
*/

static struct bt_conn_cb adv_conn_cb = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.remote_info_available = remote_info_available_cb,
};

static bool data_cb(struct bt_data *data, void *user_data){
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, NAME_LEN - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad){
	char addr_str[BT_ADDR_LE_STR_LEN];
	char name[NAME_LEN];
	int err;

	if (default_conn) {
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	(void)memset(name, 0, sizeof(name));
	bt_data_parse(ad, data_cb, name);

	if (strcmp(name, "MilliMobile")) {
		return;
	}

	if (bt_le_scan_stop()) {
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT,
				&default_conn);
	if (err) {
		printk("Create conn to %s failed (%u)\n", addr_str, err);
	}
}

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params){
	struct bt_gatt_chrc *chrc;
	char str[BT_UUID_STR_LEN];

	printk("Discovery: attr %p\n", attr);

	if (!attr) {
		return BT_GATT_ITER_STOP;
	}

	chrc = (struct bt_gatt_chrc *)attr->user_data;

	bt_uuid_to_str(chrc->uuid, str, sizeof(str));
	printk("UUID %s\n", str);

	if (!bt_uuid_cmp(chrc->uuid, &pawr_char_uuid.uuid)) {
		pawr_attr_handle = chrc->value_handle;
		printk("Characteristic handle: %d\n", pawr_attr_handle);
		k_sem_give(&sem_discovered);
	}

	return BT_GATT_ITER_STOP;
}

static void write_func(struct bt_conn *conn, uint8_t err, struct bt_gatt_write_params *params){
	if (err) {
		printk("Write failed (err %d)\n", err);
		return;
	}

	k_sem_give(&sem_written);
}

void init_bufs(void){
	for (size_t i = 0; i < ARRAY_SIZE(backing_store); i++) {
		backing_store[i][0] = ARRAY_SIZE(backing_store[i]) - 1;
		backing_store[i][1] = BT_DATA_MANUFACTURER_DATA;
		backing_store[i][2] = 0x59; /* Nordic */
		backing_store[i][3] = 0x00;

		net_buf_simple_init_with_data(&bufs[i], &backing_store[i],
					      ARRAY_SIZE(backing_store[i]));
	}
}

void adv_thread(void){
	int err;
	struct bt_le_ext_adv *pawr_adv;
	struct bt_gatt_discover_params discover_params;
	struct bt_gatt_write_params write_params;
	struct pawr_timing sync_config;

	is_shutting_down = false;
	num_synced = 0;
	for(int i = 0; i < NUM_SUBEVENTS; i ++){
		subevent_states[i].heartbeat = 0;
		subevent_states[i].free = true;
	}


	// Register the callbacks
	bt_conn_cb_register(&adv_conn_cb);

	init_bufs();
	k_poll_signal_reset(events[2].signal);

	LOG_INF("'adv_thread' has started");

	/* Create a non-connectable advertising set */
	err = bt_le_ext_adv_create(BT_LE_EXT_ADV_NCONN, &adv_cb, &pawr_adv);
	if (err) {
		LOG_ERR("Failed to create advertising set (err %d)", err);
		return ;
	}

	/* Set periodic advertising parameters */
	err = bt_le_per_adv_set_param(pawr_adv, &per_adv_params);
	if (err) {
		LOG_ERR("Failed to set periodic advertising parameters (err %d)", err);
		return ;
	}

	/* Enable Periodic Advertising */
	LOG_INF("Start Periodic Advertising");
	err = bt_le_per_adv_start(pawr_adv);
	if (err) {
		LOG_ERR("Failed to enable periodic advertising (err %d)", err);
		return ;
	}

	LOG_INF("Start Extended Advertising");
	err = bt_le_ext_adv_start(pawr_adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		LOG_ERR("Failed to start extended advertising (err %d)", err);
		return ;
	}

start_connection:
	while (num_synced < MAX_SYNCS) {

		/* Enable continuous scanning */
		err = bt_le_scan_start(BT_LE_SCAN_PASSIVE_CONTINUOUS, device_found);
		if (err) {
			LOG_WRN("Scanning failed to start (err %d)", err);
			return ;
		}

		LOG_INF("Scanning successfully started");

		/* Wait for either remote info available or involuntary disconnect */
		err = k_poll(events, ARRAY_SIZE(events), K_FOREVER);
		if(err != 0) return;		

		if (events[2].signal->signaled){
			k_poll_signal_reset(events[2].signal);
			goto mode_switch;
		}	

		err = k_sem_take(&sem_connected, K_NO_WAIT);
		if (err) {
			LOG_WRN("Disconnected before remote info available");
			goto disconnected;
		}
		
		err = bt_le_per_adv_set_info_transfer(pawr_adv, default_conn, 0);
		if (err) {
			LOG_WRN("Failed to send PAST (err %d)", err);
			goto disconnect;
		}

		LOG_INF("PAST sent");

		discover_params.uuid = &pawr_char_uuid.uuid;
		discover_params.func = discover_func;
		discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
		discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
		discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
		err = bt_gatt_discover(default_conn, &discover_params);
		if (err) {
			LOG_WRN("Discovery failed (err %d)", err);
			goto disconnect;
		}

		LOG_INF("Discovery started");

		err = k_sem_take(&sem_discovered, K_SECONDS(10));
		if (err) {
			LOG_WRN("Timed out during GATT discovery");
			goto disconnect;
		}

		check_for_lost_connections();

		// find a free subevent and allocate it to the sync
		for(int i = 0; i < NUM_SUBEVENTS; i ++){
			if(subevent_states[i].free){
				sync_config.subevent = i;
				subevent_states[i].free = false;
				subevent_states[i].heartbeat = 0;
				num_synced ++;
				break;
			}
		}

		sync_config.response_slot = 0; 

		write_params.func = write_func;
		write_params.handle = pawr_attr_handle;
		write_params.offset = 0;
		write_params.data = &sync_config;
		write_params.length = sizeof(sync_config);

		err = bt_gatt_write(default_conn, &write_params);
		if (err) {
			LOG_WRN("Write failed (err %d)", err);
			goto disconnect;
		}

		LOG_INF("Write started");

		err = k_sem_take(&sem_written, K_SECONDS(10));
		if (err) {
			LOG_WRN("Timed out during GATT write");
			goto disconnect;

		}

		LOG_WRN("PAwR config written to sync %d, disconnecting", num_synced - 1);

disconnect:
		/* Adding delay (2ms * interval value, using 2ms intead of the 1.25ms
		 * used by controller) to ensure sync is established before
		 * disconnection.
		 */
		k_sleep(K_MSEC(per_adv_params.interval_max * 2));

		err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		if (err != 0 && err != -ENOTCONN) {
			return ;
		}

disconnected:
		k_sem_take(&sem_disconnected, K_FOREVER);

		bt_conn_unref(default_conn);
		default_conn = NULL;

	}

	LOG_INF("Maximum number of syncs onboarded");
	while (num_synced == MAX_SYNCS) {
		if (check_for_lost_connections()) {
			LOG_DBG("Lost connection start scanning");
		};

		err = k_poll(&events[2], 1, K_SECONDS(10));
		if (err == 0 && events[2].signal->signaled) {
			k_poll_signal_reset(events[2].signal);
			goto mode_switch;
		}
	}
	goto start_connection;

mode_switch:
		is_shutting_down = true;

		LOG_INF("Power mode is low, exiting");
		bt_le_ext_adv_stop(pawr_adv);
		bt_le_per_adv_stop(pawr_adv);
		bt_le_ext_adv_delete(pawr_adv);
		bt_le_scan_stop();
		if (default_conn) {
			bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			bt_conn_unref(default_conn);
			default_conn = NULL;
		}

		// Unregister callbacks
		bt_conn_cb_unregister(&adv_conn_cb);
		return ;
}

