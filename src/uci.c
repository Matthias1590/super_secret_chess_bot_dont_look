#include "uci.h"
#include "search.h"
#include "move.h"
#include "types.h"
#include "generate.h"
#include "basedboard.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *get_line(FILE *stream) {
	size_t capacity = 1024;
	size_t size = 0;
	char *string = malloc(capacity);

	while (fgets(string + size, capacity - size, stream)) {
		size += strlen(string + size);

		if (string[size - 1] == '\n') {
			return string;
		}

		capacity *= 2;
		string = realloc(string, capacity);
	}

	free(string);

	return NULL;
}

char *get_token(char *string, char *store) {
	string += strlen(string);
	*string = *store;

	while (isspace(*string)) {
		string++;
	}

	if (*string) {
		char *token = string;

		while (*string && !isspace(*string)) {
			string++;
		}

		*store = *string;
		*string = '\0';

		return token;
	}

	return NULL;
}

void uci_position(struct position *pos, char *token, char *store, struct move *last_move) {
	pos->game_over = false;
	
	token = get_token(token, store);

	if (token && !strcmp(token, "startpos")) {
		parse_position(pos, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
		token = get_token(token, store);
	} else if (token && !strcmp(token, "fen")) {
		char *fen = get_token(token, store);
		int index;

		token = fen;

		for (index = 0; token && index < 5; index++) {
			token = get_token(token, store);
		}

		if (token) {
			parse_position(pos, fen);
			token = get_token(token, store);
		}
	}

	if (token && !strcmp(token, "moves")) {
		while ((token = get_token(token, store))) {
			struct move move;

			if (parse_move(&move, token) == SUCCESS) {
				if (last_move) *last_move = move;
				do_move(pos, move);
			}
		}
	}

	struct move moves[MAX_MOVES];
	if (generate_legal_moves(pos, moves) == 0) {
		pos->game_over = true;
	}
	set_bbs(pos);
}
