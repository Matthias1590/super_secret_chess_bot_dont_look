#include "basedboard.h"

void set_bbs(struct position *pos) {
	for (int c = 0; c < 2; c++) {
		for (int t = 0; t < 6; t++) {
			pos->bbs[c][t] = 0ULL;
		}
	}

	for (int index = 0; index < 64; index++) {
		int piece = pos->board[index];

		if (piece == NO_PIECE) {
			continue;
		}

		int color = COLOR(piece);
		int type = TYPE(piece);

		pos->bbs[color][type] |= 1ULL << index;
	}
}
