#pragma once

#include <stdbool.h>

typedef enum {
	WAITING_FOR_GO,
	THINKING_ON_OUR_TIME,
	THINKING_ON_THEIR_TIME,
} t_state;

extern t_state g_state;
extern bool g_cancel;
extern bool g_discard;

void discard_search(void);
void play_found_move(void);
