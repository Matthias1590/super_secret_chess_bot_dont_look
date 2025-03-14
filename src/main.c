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
#include "state.h"

/// CONFIGURATION

#define DEBUG true
#define MIN_DEPTH 2
int MAX_DEPTH = 5;

/// DEBUGGING

#if DEBUG
	size_t g_dbg_total_ponders = 0;
	size_t g_dbg_discarded_ponders = 0;

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

#if DEBUG
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
#endif

/// MAIN CODE

void update_state(void);
void start_search(void);

typedef long long t_score;
#define SCORE_MAX 100000000
#define SCORE_MIN -SCORE_MAX

struct position g_pos, g_real_pos;

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
	case WAITING_FOR_GO: {
		DEBUGF("state = WAITING_FOR_GO\n", 1);
	} break;
	case THINKING_ON_OUR_TIME: {
		DEBUGF("state = THINKING_ON_OUR_TIME\n", 1);
		// TODO: Set start_time so we can keep track of how long we've been thinking
	} break;
	case THINKING_ON_THEIR_TIME: {
		DEBUGF("state = THINKING_ON_THEIR_TIME\n", 1);
	} break;
	default: UNREACHABLE();
	}
}

bool should_stop_search(int depth) {
	update_state();

	return g_cancel && (g_discard || depth > MIN_DEPTH);
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
		default: UNREACHABLE();
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

bool is_in_check(struct position *pos) {
	struct move moves[MAX_MOVES];
	size_t moves_count = generate_pseudo_legal_moves(pos, moves);
	for (size_t i = 0; i < moves_count; i++) {
		if (TYPE(pos->board[moves[i].to_square]) == KING) {
			return true;
		}
	}
	return false;
}

bool is_check(struct position *pos, struct move move) {
	struct position copy = *pos;
	do_move(&copy, move);
	return is_in_check(&copy);
}

bool is_quiescence_move(struct position *pos, struct move move) {
	if (is_capture(pos, move)) {
		return true;
	}

	if (is_check(pos, move)) {
		return true;
	}

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

typedef struct {
	t_score score;
	struct move move;
	struct move next_move;
} t_search_res;

#define NO_MOVE ((struct move){.from_square = NO_SQUARE, .to_square = NO_SQUARE, .promotion_type = NO_TYPE})

t_search_res search_neg(t_search_res search_res) {
	return (t_search_res) {
		.score = -search_res.score,
		.move = search_res.move,
		.next_move = search_res.next_move,
	};
}

t_search_res search_res(t_score score, struct move move, struct move next_move) {
	return (t_search_res) {
		.score = score,
		.move = move,
		.next_move = next_move,
	};
}

t_search_res quiescence(t_score alpha, t_score beta) {
	t_score standpat = evaluate();
	if (standpat >= beta) {
		return search_res(beta, NO_MOVE, NO_MOVE);
	}
	if (alpha < standpat) {
		alpha = standpat;
	}

	t_search_res best_res = search_res(alpha, NO_MOVE, NO_MOVE);

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
		t_search_res res = search_neg(quiescence(-beta, -alpha));
		g_pos = copy;

		if (res.score >= beta) {
			best_res.score = beta;
			best_res.move = moves[i];
			best_res.next_move = res.move;
			break;
		}
		if (res.score > alpha) {
			alpha = res.score;
			best_res.score = alpha;
			best_res.move = moves[i];
			best_res.next_move = res.move;
		}
	}

	return best_res;
}

int g_check_counter = 0;

t_search_res negamax(int depth, t_score alpha, t_score beta) {
	if (/* g_check_counter++ % 500 == 0 && */ should_stop_search(depth)) {
		return search_res(0, NO_MOVE, NO_MOVE);
	}

	if (depth == 0) {
		return quiescence(alpha, beta);
	}

	t_search_res best_res = search_res(SCORE_MIN, NO_MOVE, NO_MOVE);

	struct move moves[MAX_MOVES];
	size_t moves_count = generate_legal_moves(&g_pos, moves);
	score_moves(&g_pos, moves, moves_count);
	sort_moves(moves, moves_count);

	if (moves_count == 0) {
		if (is_in_check(&g_pos)) {
			return search_res(-SCORE_MAX, NO_MOVE, NO_MOVE);
		} else {
			return search_res(0, NO_MOVE, NO_MOVE);
		}
	}
	best_res.move = moves[0];

	for (size_t i = 0; i < moves_count; i++) {
		// TODO: Figure out a better way to undo a move
		struct position copy = g_pos;
		do_move(&g_pos, moves[i]);
		t_search_res res = search_neg(negamax(depth - 1, -beta, -alpha));
		g_pos = copy;

		if (res.score > best_res.score) {
			best_res = res;
			best_res.move = moves[i];
			best_res.next_move = res.move;
		}
		if (res.score == SCORE_MAX) {
			break;
		}
		if (res.score >= beta) {
			best_res.score = beta;
			best_res.move = moves[i];
			best_res.next_move = res.move;
			break;
		}
		if (res.score > alpha) {
			best_res.score = res.score;
			best_res.move = moves[i];
			best_res.next_move = res.move;
			alpha = res.score;
		}
	}

	return best_res;
}

t_search_res search_at_depth(int depth) {
	return negamax(depth, SCORE_MIN, SCORE_MAX);
}

struct move g_pondering_move;

void start_pondering(void) {
#if DEBUG
	g_dbg_total_ponders++;
#endif

	do_move(&g_pos, g_pondering_move);
	set_state(THINKING_ON_THEIR_TIME);
	start_search();
}

void start_search(void) {
	g_cancel = false;
	g_discard = false;

	int depth = MIN_DEPTH;

	t_search_res last_res = search_res(0, NO_MOVE, NO_MOVE);

	do {
		t_search_res res = search_at_depth(depth);
		if (g_cancel) {
			break;
		}
		last_res = res;

		uci_printf("info depth %d score cp %lld", depth, last_res.score);

		depth++;

		if (g_state == THINKING_ON_OUR_TIME && depth > MAX_DEPTH) {
			DEBUGF("Thinking for too long, playing\n", 1);
			play_found_move();
		}
	} while (!g_cancel);

	ASSERT(depth > MIN_DEPTH || g_discard);
	ASSERT(!move_eq(last_res.move, NO_MOVE));
	DEBUGF("Search stopped\n", 1);

	// Rollback position
	g_pos = g_real_pos;

	// If the search was discarded, we were pondering the wrong move, restart the search
	if (g_discard) {
		start_search();
	// Else, play the move and start pondering again
	} else {
		ASSERT(depth >= MIN_DEPTH);  // We should have searched at least at the min depth

		char buffer[6];

		buffer[0] = 'a' + FILE(last_res.move.from_square);
		buffer[1] = '1' + RANK(last_res.move.from_square);
		buffer[2] = 'a' + FILE(last_res.move.to_square);
		buffer[3] = '1' + RANK(last_res.move.to_square);
		buffer[4] = "\0pnbrqk"[last_res.move.promotion_type + 1];
		buffer[5] = '\0';

		uci_printf("bestmove %s", buffer);
		do_move(&g_pos, last_res.move);

		buffer[0] = 'a' + FILE(last_res.next_move.from_square);
		buffer[1] = '1' + RANK(last_res.next_move.from_square);
		buffer[2] = 'a' + FILE(last_res.next_move.to_square);
		buffer[3] = '1' + RANK(last_res.next_move.to_square);
		buffer[4] = "\0pnbrqk"[last_res.next_move.promotion_type + 1];
		buffer[5] = '\0';

		uci_printf("info string pondering %s", buffer);

#if DEBUG
		struct move moves[MAX_MOVES];
		size_t moves_count = generate_legal_moves(&g_pos, moves);

		// Check if predicted move is legal
		bool found = moves_count > 0;
		for (size_t i = 0; i < moves_count; i++) {
			if (move_eq(moves[i], last_res.next_move)) {
				found = true;
				break;
			}
		}

		ASSERT(found);
#endif

		// Done thinking, start pondering
		g_pondering_move = last_res.next_move;
		start_pondering();
	}
}

// This function gets called if we pondered the wrong move
void restart_search(void) {
#if DEBUG
	g_dbg_discarded_ponders++;
#endif
	discard_search();
	set_state(THINKING_ON_OUR_TIME);
}

struct move g_last_move;

void handle_position(char *token, char *store) {
	uci_position(&g_real_pos, token, store, &g_last_move);
	g_pos = g_real_pos;
}

void handle_go(char *token, char *store) {
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

	// if we have less than 10 seconds left, we switch the max depth to 3
	if (info.time[g_pos.side_to_move] < 10000) {
		printf("info blitz mode activated\n");
		MAX_DEPTH = 3;
	}

	switch (g_state) {
	case WAITING_FOR_GO: {
		set_state(THINKING_ON_OUR_TIME);
		start_search();
	} break;
	case THINKING_ON_OUR_TIME: {
		UNREACHABLE();  // We're already thinking, we shouldn't be getting another go command
	} break;
	case THINKING_ON_THEIR_TIME: {
		// If we we're pondering the wrong move, restart the search
		if (!move_eq(g_last_move, g_pondering_move)) {
			restart_search();
		} else {
			// If we're here it means we were pondering the correct move.
			// We're gonna keep thinking for a little while longer and then play
			set_state(THINKING_ON_OUR_TIME);
		}
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

	while (!g_pos.game_over) {
		update_state();
	}

#if DEBUG
	DEBUGF("Total ponders: %zu\n", g_dbg_total_ponders);
	DEBUGF("Discarded ponders: %zu\n", g_dbg_discarded_ponders);
	DEBUGF("Correct ponder percentage: %f\n", 1.0 - (double)g_dbg_discarded_ponders / g_dbg_total_ponders);
#endif

	return 0;
}
