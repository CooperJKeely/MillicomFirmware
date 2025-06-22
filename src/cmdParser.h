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

int parse_command(void);
int get_temperature(void);
#endif
