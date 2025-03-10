#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/poll.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#include <dlfcn.h>
#include "parse.h"
#include "position.h"
#include "search.h"
#include "types.h"
#include "generate.h"
#include "basedboard.h"
#include "uci.h"
#include "pst.h"

/// CONFIGURATION

#define DEBUG true
#define MIN_DEPTH 2
#define MAX_DEPTH 6

/// DEBUGGING

#if DEBUG
	FILE *g_debug_file = NULL;

	#define ASSERT(condition) \
		do { \
			if (!(condition)) { \
				fprintf(g_debug_file, "%s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #condition); \
				fprint_trace(g_debug_file); \
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

static void uci_printf(char *format, ...) {
    va_list args;
    va_list args_copy;
    va_start(args, format);
	va_copy(args_copy, args);

    // Print to stdout
    vfprintf(stdout, format, args);
	fprintf(stdout, "\n");
	fflush(stdout);

#if DEBUG
    // Print to stderr
	DEBUGF("> '", 1);
    vfprintf(g_debug_file, format, args_copy);
	fprintf(g_debug_file, "'\n");
	fflush(g_debug_file);
#endif

    va_end(args);
    va_end(args_copy);
}

static bool streq(char *a, char *b) {
	return strcmp(a, b) == 0;
}

static void signal_handler(int signum) {
	DEBUGF("Signal %d received\n", signum);
}

static void fprint_trace(FILE *f) {
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    Dl_info info;
    char cmd[256];

    for (int i = 0; i < frames; ++i) {
        if (dladdr(callstack[i], &info) && info.dli_fname) {
            fprintf(f, "%s:", info.dli_fname);
            if (info.dli_sname) {
                fprintf(f, "%s\n", info.dli_sname);
            } else {
                fprintf(f, "??\n");
            }

            // Use addr2line to get line numbers
            snprintf(cmd, sizeof(cmd), "addr2line -e %s %p", info.dli_fname, callstack[i]);
            FILE *pipe = popen(cmd, "r");
            if (pipe) {
                char line[128];
                if (fgets(line, sizeof(line), pipe)) {
                    fprintf(f, "    at %s", line);
                }
                pclose(pipe);
            }
        } else {
            fprintf(f, "??\n");
        }
    }
}

/// MAIN CODE

void update_state(void);

typedef long long t_score;
#define SCORE_MAX 100000000
#define SCORE_MIN -SCORE_MAX

struct position g_pos, g_rollback_pos;

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
	case WAITING: {
		DEBUGF("state = WAITING\n", 1);
	} break;
	case THINKING: {
		DEBUGF("state = THINKING\n", 1);
		// TODO: Set start_time so we can keep track of how long we've been thinking
	} break;
	case PONDERING: {
		DEBUGF("state = PONDERING\n", 1);
	} break;
	default: UNREACHABLE();
	}
}

bool should_cancel_search(int depth) {
	if (depth <= MIN_DEPTH) {
		return false;
	}

	update_state();

	return g_cancel;
}

t_score get_piece_value(int type) {
	switch (type) {
		case PAWN: return 100;
		case KNIGHT: return 320;
		case BISHOP: return 330;
		case ROOK: return 500;
		case QUEEN: return 900;
		case KING: return 20000;
		default: UNREACHABLE();
	}
}

t_score get_square_value(int piece, int square) {
	ASSERT(piece != NO_PIECE);

	switch (TYPE(piece)) {
		case PAWN:
			return pawn_squares_mid[COLOR(piece) == WHITE ? 63 - square : square];
		case KNIGHT:
			return knight_squares_mid[COLOR(piece) == WHITE ? 63 - square : square];
		case BISHOP:
			return bishop_squares_mid[COLOR(piece) == WHITE ? 63 - square : square];
		case ROOK:
			return rook_squares_mid[COLOR(piece) == WHITE ? 63 - square : square];
		case QUEEN:
			return queen_squares_mid[COLOR(piece) == WHITE ? 63 - square : square];
		case KING:
			return king_squares_mid[COLOR(piece) == WHITE ? 63 - square : square];
	}
}

struct move g_null_moves[MAX_MOVES];

t_score evaluate(void) {
	t_score score = 0;

	// Material count
	score += bb_count(g_pos.bbs[WHITE][PAWN]) * get_piece_value(PAWN);
	score -= bb_count(g_pos.bbs[BLACK][PAWN]) * get_piece_value(PAWN);
	score += bb_count(g_pos.bbs[WHITE][KNIGHT]) * get_piece_value(KNIGHT);
	score -= bb_count(g_pos.bbs[BLACK][KNIGHT]) * get_piece_value(KNIGHT);
	score += bb_count(g_pos.bbs[WHITE][BISHOP]) * get_piece_value(BISHOP);
	score -= bb_count(g_pos.bbs[BLACK][BISHOP]) * get_piece_value(BISHOP);
	score += bb_count(g_pos.bbs[WHITE][ROOK]) * get_piece_value(ROOK);
	score -= bb_count(g_pos.bbs[BLACK][ROOK]) * get_piece_value(ROOK);
	score += bb_count(g_pos.bbs[WHITE][QUEEN]) * get_piece_value(QUEEN);
	score -= bb_count(g_pos.bbs[BLACK][QUEEN]) * get_piece_value(QUEEN);
	score += bb_count(g_pos.bbs[WHITE][KING]) * get_piece_value(KING);
	score -= bb_count(g_pos.bbs[BLACK][KING]) * get_piece_value(KING);

	// Piece square tables
	for (int i = 0; i < 64; i++) {
		if (g_pos.board[i] == NO_PIECE) {
			continue;
		}

		int color = COLOR(g_pos.board[i]);
		score += get_square_value(g_pos.board[i], i) * (color == WHITE ? 1 : -1);
	}

	// Pins
	// TODO
	// piece of value x is blocking a piece of value >= x, and is attacked by a piece of value < x

	// Mobility
	// TODO
	// score += generate_legal_moves(&g_pos, g_null_moves) * (g_pos.side_to_move == WHITE ? 1 : -1);

	return score * (g_pos.side_to_move == WHITE ? 1 : -1);
}

bool is_capture(struct position *pos, struct move move) {
	return pos->board[move.to_square] != NO_PIECE;
}

bool is_quiescence_move(struct position *pos, struct move move) {
	if (is_capture(pos, move)) {
		return true;
	}

	// TODO: Add checks
	// ...

	return false;
}

// TODO: Improve
long long score_move(struct position *pos, struct move move) {
	long long score = 0;

	if (is_capture(pos, move)) {
		score += get_piece_value(TYPE(pos->board[move.to_square]));
	}

	// TODO: Add double check and regular checks
	// if (is_double_check(pos, move)) {
	// 	score += 100;
	// }

	// new piece square value
	int current_square_value = get_square_value(g_pos.board[move.from_square], move.from_square);
	int new_square_value = get_square_value(g_pos.board[move.from_square], move.to_square);
	score += new_square_value - current_square_value;

	return score;
}

int compare_moves(struct move *a, struct move *b) {
	return b->score - a->score;
}

bool move_eq(struct move a, struct move b) {
	return a.from_square == b.from_square && a.to_square == b.to_square && a.promotion_type == b.promotion_type;
}

void score_moves(struct position *pos, struct move *moves, size_t count) {
	for (size_t i = 0; i < count; i++) {
		moves[i].score = score_move(pos, moves[i]);
	}
}

void sort_moves(struct move *moves, size_t count) {
	qsort(moves, count, sizeof(struct move), (int (*)(const void *, const void *))compare_moves);
}

t_score quiescence(t_score alpha, t_score beta) {
	t_score standpat = evaluate();
	if (standpat >= beta) {
		return beta;
	}
	if (alpha < standpat) {
		alpha = standpat;
	}

	struct move moves[MAX_MOVES];
	size_t moves_count = generate_legal_moves(&g_pos, moves);
	score_moves(&g_pos, moves, moves_count);
	sort_moves(moves, moves_count);
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

int g_check_counter = 0;

t_score negamax(int depth, t_score alpha, t_score beta, struct move *best_move, struct move *best_opponent_move) {
	if (/* g_check_counter++ % 500 == 0 && */ should_cancel_search(depth)) {
		return 0;
	}

	if (depth == 0) {
		return quiescence(alpha, beta);
	}

	t_score best_score = SCORE_MIN;
	struct move best_local_move, best_local_opponent_move;

	struct move moves[MAX_MOVES];
	size_t moves_count = generate_legal_moves(&g_pos, moves);
	score_moves(&g_pos, moves, moves_count);
	sort_moves(moves, moves_count);

	if (best_move) {
		*best_move = moves[0];
	}

	for (size_t i = 0; i < moves_count; i++) {
		// TODO: Figure out a better way to undo a move
		struct position copy = g_pos;
		do_move(&g_pos, moves[i]);
		t_score score;
		if (best_move && best_opponent_move) {
			score = -negamax(depth - 1, -beta, -alpha, &best_local_opponent_move, NULL);
		} else {
			score = -negamax(depth - 1, -beta, -alpha, NULL, NULL);
		}
		g_pos = copy;

		if (score == SCORE_MAX) {
			best_score = score;
			best_local_move = moves[i];
			if (best_opponent_move) {
				*best_opponent_move = best_local_opponent_move;
			}
			break;
		}
		if (score > best_score) {
			best_score = score;
			best_local_move = moves[i];
			if (best_opponent_move) {
				*best_opponent_move = best_local_opponent_move;
			}
		}
		if (score >= beta) {
			best_score = beta;
			best_local_move = moves[i];
			if (best_opponent_move) {
				*best_opponent_move = best_local_opponent_move;
			}
			break;
		}
		if (score > alpha) {
			best_score = score;
			best_local_move = moves[i];
			if (best_opponent_move) {
				*best_opponent_move = best_local_opponent_move;
			}
			alpha = score;
		}
	}

	if (best_move) {
		*best_move = best_local_move;
	}
	return best_score;
}

t_score search_at_depth(int depth, struct move *best_move, struct move *best_opponent_move) {
	return negamax(depth, SCORE_MIN, SCORE_MAX, best_move, best_opponent_move);
}

struct move g_pondering_move;

void start_search(void) {
	int depth = MIN_DEPTH;

	t_score last_score = 0;
	struct move last_best_move, last_best_opponent_move;

	while (true) {
		struct move best_move, best_opponent_move;
		t_score score = search_at_depth(depth, &best_move, &best_opponent_move);

		if (g_cancel) {
			break;
		}

		uci_printf("info depth %d score cp %lld", depth, score);

		last_score = score;
		last_best_move = best_move;
		last_best_opponent_move = best_opponent_move;

		depth++;

		if (g_state != PONDERING && depth > MAX_DEPTH) {
			break;
		}
	}

	// Restore position
	g_pos = g_rollback_pos;

	bool was_cancelled = g_cancel;
	if (was_cancelled) {
		DEBUGF("Search was cancelled\n", 1);
	}
	g_cancel = false;

	// Pondering can be cancelled but the state should be set to WAITING when its cancelled
	ASSERT(g_state != PONDERING);

	if (g_state != WAITING) {
		ASSERT(depth >= MIN_DEPTH);  // We should have searched at least once

		char buffer[5] = {0};
		buffer[0] = 'a' + FILE(last_best_move.from_square);
		buffer[1] = '1' + RANK(last_best_move.from_square);
		buffer[2] = 'a' + FILE(last_best_move.to_square);
		buffer[3] = '1' + RANK(last_best_move.to_square);
		buffer[4] = "\0pnbrqk"[last_best_move.promotion_type + 1];

		uci_printf("bestmove %s", buffer);
		do_move(&g_pos, last_best_move);

		buffer[0] = 'a' + FILE(last_best_opponent_move.from_square);
		buffer[1] = '1' + RANK(last_best_opponent_move.from_square);
		buffer[2] = 'a' + FILE(last_best_opponent_move.to_square);
		buffer[3] = '1' + RANK(last_best_opponent_move.to_square);
		buffer[4] = "\0pnbrqk"[last_best_opponent_move.promotion_type + 1];

		uci_printf("predicted opponent move %s", buffer);

#if DEBUG
		struct move moves[MAX_MOVES];
		size_t moves_count = generate_legal_moves(&g_pos, moves);

		// Check if predicted move is legal
		bool found = moves_count > 0;
		for (size_t i = 0; i < moves_count; i++) {
			if (move_eq(moves[i], last_best_opponent_move)) {
				found = true;
				break;
			}
		}

		ASSERT(found);
#endif

		// Done thinking, start pondering
		set_state(PONDERING);
		g_pondering_move = last_best_opponent_move;
		do_move(&g_pos, g_pondering_move);
		start_search();
	}
}

void handle_position(char *token, char *store) {
	struct move last_move;
	uci_position(&g_pos, token, store, &last_move);
	g_rollback_pos = g_pos;

	switch (g_state) {
	case WAITING: break;
	case THINKING: {
		UNREACHABLE();  // It's our turn to move and we only move after we're done thinking, this shouldn't happen
	} break;
	case PONDERING: {
		if (!move_eq(last_move, g_pondering_move)) {
			// Cancel pondering and go to waiting stage, wait for go command
			g_cancel = true;
			set_state(WAITING);
		} else {
			// We predicted the opponents move correctly!
			DEBUGF("Predicted opponent move correctly\n", 1);
			set_state(THINKING);
		}
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
		// If we're here it means we were pondering the correct move.
		// Cancel the search and play it instantly
		g_cancel = true;
	} break;
	case PONDERING: {
		UNREACHABLE();  // If we're pondering the correct move, the state will be set to THINKING, we should never get here.
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
			if (streq(token, "quit")) {
				exit(0);
			} else if (streq(token, "uci")) {
				uci_printf("id name checkmate.exe");
				uci_printf("id author amel-fou mapatenk mwijnsma");
				uci_printf("uciok");
			} else if (streq(token, "isready")) {
				uci_printf("readyok");
			} else if (streq(token, "position")) {
				handle_position(token, &store);
			} else if (streq(token, "go")) {
				handle_go(token, &store);
			} else if (streq(token, "setoption")) {
				break;
			} else if (streq(token, "register")) {
				break;
			} else {
				continue;
			}

			break;
		}

		free(line);
		fflush(stdout);
	}
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
