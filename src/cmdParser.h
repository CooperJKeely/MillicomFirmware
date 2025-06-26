#ifndef CMDPARSER_H
#define CMDPARSER_H

/*
// Duplicate from main.c for reference

typedef enum{
	CMD_TEMP,			// 0
	CMD_STANDBY,		// 1
	CMD_CAPACITOR,		// 2
} cmd_mode_t;
*/

int8_t parse_command(void);
int8_t get_temperature(void);
#endif
