#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/poll.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include "parse.h"
#include "position.h"
#include "search.h"
#include "types.h"
#include "generate.h"
#include "uci.h"

/// CONFIGURATION

#define DEBUG true
#define MIN_DEPTH 2

/// DEBUGGING

#if DEBUG
	FILE *g_debug_file = NULL;

	#define ASSERT(condition) \
		do { \
			if (!(condition)) { \
				fprintf(g_debug_file, "%s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #condition); \
				fflush(g_debug_file); \
				abort(); \
			} \
		} while (0)
	#define UNREACHABLE() ASSERT(false)
	#define TODO() ASSERT(false)
	#define DEBUGF(fmt, ...) \
		do { \
			fprintf(g_debug_file, "%s:%d: ", __FILE__, __LINE__); \
			fprintf(g_debug_file, fmt, __VA_ARGS__); \
			fflush(g_debug_file); \
		} while (0)
#else
	#define ASSERT(condition) \
		do { \
			if (!(condition)) { \
				UNREACHABLE(); \
			} \
		} while (0)
	#define UNREACHABLE() __builtin_unreachable()
	#define TODO() __builtin_unreachable()
	#define DEBUGF(...)
#endif

/// UTILITIES

static bool streq(char *a, char *b) {
	return strcmp(a, b) == 0;
}

/// MAIN CODE

void update_state(void);

typedef long long t_score;
#define SCORE_MAX 100000000
#define SCORE_MIN -SCORE_MAX

struct position g_pos;

typedef enum {
	WAITING,
	THINKING,
	PONDERING,
} t_state;

t_state g_state = WAITING;
bool g_cancel = false;

char *g_commands[1024];
size_t g_commands_head = 0;
size_t g_commands_tail = 0;

char *read_command(void) {
	char *line = malloc(1024);
	ASSERT(line != NULL);

	size_t size = 0;
	while (read(STDIN_FILENO, line + size, 1) > 0) {
		if (line[size] == '\n') {
			break;
		}

		size++;
	}
	line[size] = '\0';

	return line;
}

void enqueue_commands(void) {
	struct pollfd fds = { .fd = 0, .events = POLLIN, .revents = 0 };
	while (poll(&fds, 1, 0) > 0 && (fds.revents & POLLIN)) {
		char *line = read_command();
		ASSERT(line != NULL);

		DEBUGF("< '%s'\n", line);

		g_commands[g_commands_tail] = line;
		g_commands_tail = (g_commands_tail + 1) % 1024;

		ASSERT(g_commands_tail != g_commands_head);
	}
}

bool command_available(void) {
	return g_commands_head != g_commands_tail;
}

char *dequeue_command(void) {
	ASSERT(command_available());

	char *command = g_commands[g_commands_head];
	g_commands_head = (g_commands_head + 1) % 1024;

	return command;
}

void set_state(t_state state) {
	g_state = state;
	switch (state) {
	case WAITING: break;
	case THINKING: {
		// TODO: Set start_time so we can keep track of how long we've been thinking
	} break;
	case PONDERING: break;
	default: UNREACHABLE();
	}
}

bool should_cancel_search(void) {
	update_state();

	return g_cancel;
}

t_score evaluate(void) {
	// Material count
	t_score score = 0;

	for (int i = 0; i < 64; i++) {
		if (g_pos.board[i] == NO_PIECE) {
			continue;
		}

		switch (TYPE(g_pos.board[i])) {
		case PAWN:
			score += COLOR(g_pos.board[i]) == WHITE ? 100 : -100;
			break;
		case KNIGHT:
			score += COLOR(g_pos.board[i]) == WHITE ? 320 : -320;
			break;
		case BISHOP:
			score += COLOR(g_pos.board[i]) == WHITE ? 330 : -330;
			break;
		case ROOK:
			score += COLOR(g_pos.board[i]) == WHITE ? 500 : -500;
			break;
		case QUEEN:
			score += COLOR(g_pos.board[i]) == WHITE ? 900 : -900;
			break;
		case KING:
			score += COLOR(g_pos.board[i]) == WHITE ? 20000 : -20000;
			break;
		}
	}

	return score * (g_pos.side_to_move == WHITE ? 1 : -1);
}

bool is_quiescence_move(struct position *pos, struct move move) {
	// TODO: Implement
	return false;
}

bool is_capture(struct move move) {
	return g_pos.board[move.to_square] != NO_PIECE;
}

// TODO: Improve
int score_move(struct move move) {
	int score = 0;

	if (is_capture(move)) {
		score += 10;
	}

	return score;
}

int compare_moves(struct move *a, struct move *b) {
	return b->score - a->score;
}

void sort_moves(struct move *moves, size_t count) {
	for (size_t i = 0; i < count; i++) {
		moves[i].score = score_move(moves[i]);
	}

	qsort(moves, count, sizeof(struct move), (int (*)(const void *, const void *))compare_moves);
}

t_score quiescence(t_score alpha, t_score beta) {
	if (should_cancel_search()) {
		return 0;
	}

	t_score standpat = evaluate();
	if (standpat >= beta) {
		return beta;
	}
	if (alpha < standpat) {
		alpha = standpat;
	}

	struct move moves[MAX_MOVES];
	size_t moves_count = generate_legal_moves(&g_pos, moves);
	for (size_t i = 0; i < moves_count; i++) {
		if (!is_quiescence_move(&g_pos, moves[i])) {
			continue;
		}

		// TODO: Figure out a better way to undo a move
		struct position copy = g_pos;
		do_move(&g_pos, moves[i]);
		t_score score = -quiescence(-beta, -alpha);
		g_pos = copy;

		if (score >= beta) {
			return beta;
		}
		if (score > alpha) {
			alpha = score;
		}
	}

	return alpha;
}

t_score negamax(int depth, t_score alpha, t_score beta, struct move *best_move) {
	if (should_cancel_search()) {
		return 0;
	}

	if (depth == 0) {
		return quiescence(alpha, beta);
	}

	t_score best_score = SCORE_MIN;
	struct move best_local_move;

	struct move moves[MAX_MOVES];
	size_t moves_count = generate_legal_moves(&g_pos, moves);
	sort_moves(moves, moves_count);
	for (size_t i = 0; i < moves_count; i++) {
		// TODO: Figure out a better way to undo a move
		struct position copy = g_pos;
		do_move(&g_pos, moves[i]);
		t_score score = -negamax(depth - 1, -beta, -alpha, NULL);
		g_pos = copy;

		if (score == SCORE_MAX) {
			best_score = score;
			best_local_move = moves[i];
			break;
		}
		if (score > best_score) {
			best_score = score;
			best_local_move = moves[i];
		}
		if (score >= beta) {
			best_score = beta;
			best_local_move = moves[i];
			break;
		}
		if (score > alpha) {
			best_score = score;
			best_local_move = moves[i];
			alpha = score;
		}
	}

	if (best_move) {
		*best_move = best_local_move;
	}
	return best_score;
}

t_score search_at_depth(int depth, struct move *best_move) {
	return negamax(depth, SCORE_MIN, SCORE_MAX, best_move);
}

void start_search(void) {
	int depth = MIN_DEPTH;
	struct position pos = g_pos;

	t_score last_score = 0;
	struct move last_move;

	while (true) {
		struct move best_move;
		t_score score = search_at_depth(depth, &best_move);

		if (g_cancel) {
			break;
		}

		printf("info depth %d score cp %lld\n", depth, score);

		last_score = score;
		last_move = best_move;

		depth++;

		if (depth == 5) {
			break;
		}
	}

	// Restore position
	g_cancel = false;
	g_pos = pos;

	ASSERT(depth > MIN_DEPTH);  // We should have searched at least once

	char buffer[5] = {0};
	buffer[0] = 'a' + FILE(last_move.from_square);
	buffer[1] = '1' + RANK(last_move.from_square);
	buffer[2] = 'a' + FILE(last_move.to_square);
	buffer[3] = '1' + RANK(last_move.to_square);
	buffer[4] = "\0pnbrqk"[last_move.promotion_type + 1];

	printf("bestmove %s\n", buffer);

	// TODO: Start pondering
	set_state(WAITING);
}

void handle_position(char *token, char *store) {
	switch (g_state) {
	case WAITING: {
		uci_position(&g_pos, token, store);
	} break;
	case THINKING: {
		UNREACHABLE();  // It's our turn to move and we only move after we're done thinking, this shouldn't happen
	} break;
	case PONDERING: {
		// Check if the position is the same as the one we're pondering, if so, continue pondering, otherwise, cancel pondering
		TODO();
	} break;
	default: UNREACHABLE();
	}
}

void handle_go(char *token, char *store) {
	switch (g_state) {
	case WAITING: {
		struct search_info info;

		info.pos = &g_pos;
		info.time[WHITE] = 0;
		info.time[BLACK] = 0;
		info.increment[WHITE] = 0;
		info.increment[BLACK] = 0;

		while ((token = get_token(token, store))) {
			if (!strcmp(token, "searchmoves")) {
				break;
			} else if (!strcmp(token, "ponder")) {
				continue;
			} else if (!strcmp(token, "infinite")) {
				continue;
			} else if (!strcmp(token, "wtime")) {
				token = get_token(token, store);
				info.time[WHITE] = token ? atoi(token) : 0;
			} else if (!strcmp(token, "btime")) {
				token = get_token(token, store);
				info.time[BLACK] = token ? atoi(token) : 0;
			} else if (!strcmp(token, "winc")) {
				token = get_token(token, store);
				info.increment[WHITE] = token ? atoi(token) : 0;
			} else if (!strcmp(token, "binc")) {
				token = get_token(token, store);
				info.increment[BLACK] = token ? atoi(token) : 0;
			} else {
				token = get_token(token, store);
			}

			if (!token) {
				break;
			}
		}

		(void)info;  // TODO: Use info

		set_state(THINKING);
		start_search();
	} break;
	case THINKING: {
		UNREACHABLE();  // We're already thinking, this shouldn't happen
	} break;
	case PONDERING: {
		// We're pondering the correct position
		set_state(THINKING);
	} break;
	default: UNREACHABLE();
	}
}

void update_state(void) {
	enqueue_commands();

	char *line = NULL;
	while (command_available() && (line = dequeue_command())) {
		char *token = line;
		char store = *token;

		*token = '\0';

		while ((token = get_token(token, &store))) {
			if (!strcmp(token, "quit")) {
				exit(0);
			} else if (!strcmp(token, "uci")) {
				printf("id name %s\n", "name");
				printf("id author %s\n", "author");
				printf("uciok\n");
			} else if (!strcmp(token, "isready")) {
				printf("readyok\n");
			} else if (!strcmp(token, "position")) {
				// uci_position(&g_pos, token, &store);
			} else if (!strcmp(token, "go")) {
				// uci_go(&g_pos, token, &store);
			} else if (!strcmp(token, "setoption")) {
				break;
			} else if (!strcmp(token, "register")) {
				break;
			} else {
				continue;
			}

			break;
		}

		free(line);
		fflush(stdout);
	}

	// enqueue_commands();

	// char *line = NULL;
	// while (command_available() && (line = dequeue_command())) {
	// 	DEBUGF("> %s", line);

	// 	char *token = line;
	// 	char store = *token;

	// 	*token = '\0';

	// 	while ((token = get_token(token, &store))) {
	// 		if (!strcmp(token, "quit")) {
	// 			exit(0);
	// 		} else if (!strcmp(token, "uci")) {
	// 			printf("id name %s\n", "name");
	// 			printf("id author %s\n", "author");
	// 			printf("uciok\n");
	// 		} else if (!strcmp(token, "isready")) {
	// 			printf("readyok\n");
	// 		} else if (!strcmp(token, "position")) {
	// 			uci_position(&g_pos, token, &store);
	// 		} else if (!strcmp(token, "go")) {
	// 			// uci_go(&g_pos, token, &store);
	// 		} else if (!strcmp(token, "setoption")) {
	// 			break;
	// 		} else if (!strcmp(token, "register")) {
	// 			break;
	// 		} else {
	// 			continue;
	// 		}

	// 		break;
	// 	}

	// 	// if (streq(token, "quit")) {
	// 	// 	DEBUGF("Exiting...\n");
	// 	// 	exit(0);
	// 	// } else if (streq(token, "stop")) {
	// 	// 	g_cancel = true;
	// 	// } else if (streq(token, "uci")) {
	// 	// 	printf("id name checkmate.exe\n");
	// 	// 	printf("id author Ama Misha Matthias\n");
	// 	// 	printf("uciok\n");
	// 	// } else if (streq(token, "isready")) {
	// 	// 	printf("readyok\n");
	// 	// } else if (streq(token, "position")) {
	// 	// 	// handle_position(token, &store);
	// 	// } else if (streq(token, "go")) {
	// 	// 	// handle_go(token, &store);
	// 	// } else {
	// 	// 	// Ignore
	// 	// }

	// 	// printf("info string handled token %s\n", token);
	// 	// DEBUGF("handled %s\n", line);

	// 	// free(line);
	// 	// fflush(stdout);
	// }
}

int main(void) {
#if DEBUG
	g_debug_file = fopen("debug.log", "w");
	ASSERT(g_debug_file != NULL);
#endif

	while (true) {
		update_state();
	}

	return 0;
}
