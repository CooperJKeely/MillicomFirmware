#ifndef CMDPARSER_H
#define CMDPARSER_H
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_vs.h>

// command variable from main
extern uint8_t command;

uint8_t parse_command();
int8_t get_chip_temperature(void);


#endif
