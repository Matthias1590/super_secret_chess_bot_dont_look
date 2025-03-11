#include "state.h"

t_state g_state = WAITING_FOR_GO;
bool g_cancel = false;
bool g_discard = false;

static void stop_search(bool discard) {
	g_cancel = true;
	g_discard = discard;
}

void discard_search() {
	stop_search(true);
}

void play_found_move(void) {
	stop_search(false);
}
