#ifndef CMDPARSER_H
#define CMDPARSER_H
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_vs.h>

uint8_t parse_command(uint8_t command);
int8_t get_temperature(void);
#endif
