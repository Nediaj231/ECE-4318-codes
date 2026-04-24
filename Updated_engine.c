#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define ENGINE_VERSION "v2.1.0"

// ============================================================
// REPETITION DETECTION
// ============================================================
// Simple position hash for repetition tracking.
// We store a lightweight hash of each position played.
static unsigned long long g_pos_hashes[1024]; // hash of each position in game
static int g_pos_hash_count = 0;

// Quick hash of a board position (not Zobrist, but sufficient for repetition)
static unsigned long long compute_pos_hash(const char board[64], int wtm, int castle_bits) {
    unsigned long long h = (unsigned long long)wtm;
    h = h * 1099511628211ULL + (unsigned char)castle_bits;
    for (int i = 0; i < 64; i++) {
        h = h * 1099511628211ULL + (unsigned char)board[i];
    }
    return h;
}

// Check if a position hash has been seen 'threshold' times in game history
static int is_repetition(unsigned long long hash, int threshold) {
    int count = 0;
    for (int i = 0; i < g_pos_hash_count; i++) {
        if (g_pos_hashes[i] == hash) {
            count++;
            if (count >= threshold) return 1;
        }
    }
    return 0;
}

// ============================================================
// OPENING BOOK
// ============================================================
// Move history tracking for book lookups
static char g_move_history[512][6]; // UCI moves played so far
static int  g_move_count = 0;

typedef struct { const char *line; const char *reply; } BookEntry;

// 24 opening lines covering major chess openings.
// "line" = space-separated UCI moves already played.
// "reply" = the book move to play next.
static const BookEntry OPENING_BOOK[] = {
    // ── White first moves (randomly selected!) ──
    {"",                                        "e2e4"},  // 1. e4 (King's Pawn)
    {"",                                        "d2d4"},  // 1. d4 (Queen's Pawn)
    {"",                                        "g1f3"},  // 1. Nf3 (Réti Opening)

    // ── Italian Game: 1.e4 e5 2.Nf3 Nc6 3.Bc4 ──
    {"e2e4 e7e5",                                "g1f3"},  // 2. Nf3
    {"e2e4 e7e5 g1f3 b8c6",                      "f1c4"},  // 3. Bc4 (Italian)
    {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5",            "c2c3"},  // 4. c3 (Giuoco Piano)
    {"e2e4 e7e5 g1f3 b8c6 f1c4 g8f6",            "d2d3"},  // 4. d3 (Giuoco Pianissimo)

    // ── Ruy Lopez: 1.e4 e5 2.Nf3 Nc6 3.Bb5 ──
    {"e2e4 e7e5 g1f3 b8c6",                      "f1b5"},  // 3. Bb5 (Ruy Lopez alternative)
    {"e2e4 e7e5 g1f3 b8c6 f1b5 a7a6",            "b5a4"},  // 4. Ba4

    // ── Sicilian Defense: 1.e4 c5 ──
    {"e2e4 c7c5",                                "g1f3"},  // 2. Nf3 (Open Sicilian)
    {"e2e4 c7c5 g1f3 d7d6",                      "d2d4"},  // 3. d4
    {"e2e4 c7c5 g1f3 b8c6",                      "d2d4"},  // 3. d4
    {"e2e4 c7c5 g1f3 e7e6",                      "d2d4"},  // 3. d4

    // ── French Defense: 1.e4 e6 ──
    {"e2e4 e7e6",                                "d2d4"},  // 2. d4
    {"e2e4 e7e6 d2d4 d7d5",                      "b1c3"},  // 3. Nc3 (Classical)

    // ── Caro-Kann: 1.e4 c6 ──
    {"e2e4 c7c6",                                "d2d4"},  // 2. d4
    {"e2e4 c7c6 d2d4 d7d5",                      "b1c3"},  // 3. Nc3

    // ── Scandinavian: 1.e4 d5 ──
    {"e2e4 d7d5",                                "e4d5"},  // 2. exd5

    // ── Black responses to 1.e4 (randomly selected!) ──
    {"e2e4",                                     "e7e5"},  // 1...e5 (classical)
    {"e2e4",                                     "c7c5"},  // 1...c5 (Sicilian)
    {"e2e4",                                     "e7e6"},  // 1...e6 (French)
    {"e2e4 e7e5 g1f3",                           "b8c6"},  // 2...Nc6
    {"e2e4 e7e5 g1f3 b8c6 f1b5",                 "a7a6"},  // 3...a6 (Morphy Defense)
    {"e2e4 e7e5 g1f3 b8c6 f1c4",                 "f8c5"},  // 3...Bc5

    // ── Queen's Gambit: 1.d4 d5 2.c4 ──
    {"d2d4 d7d5",                                "c2c4"},  // 2. c4 (Queen's Gambit)
    {"d2d4 d7d5 c2c4 e7e6",                      "b1c3"},  // 3. Nc3 (QGD)

    // ── London System: 1.d4 Nf6 2.Bf4 ──
    {"d2d4 g8f6",                                "c1f4"},  // 2. Bf4 (London)
    {"d2d4 g8f6",                                "c2c4"},  // 2. c4 (alternative)

    // ── Black responses to 1.d4 (randomly selected!) ──
    {"d2d4",                                     "d7d5"},  // 1...d5
    {"d2d4",                                     "g8f6"},  // 1...Nf6 (Indian Defense)
    {"d2d4 d7d5 c2c4",                           "e7e6"},  // 2...e6 (QGD)
    {"d2d4 d7d5 c2c4",                           "c7c6"},  // 2...c6 (Slav Defense)

    // ── Réti / English responses ──
    {"g1f3",                                     "d7d5"},  // 1...d5
    {"g1f3",                                     "g8f6"},  // 1...Nf6
    {"c2c4",                                     "e7e5"},  // 1...e5
    {"c2c4",                                     "g8f6"},  // 1...Nf6
};
#define BOOK_SIZE (sizeof(OPENING_BOOK) / sizeof(OPENING_BOOK[0]))

// Build history string and look up in book. Returns 1 if found.
// If multiple book moves exist for the same position, picks randomly.
static int book_lookup(char *out_move) {
    // Build current history string
    char history[2048] = "";
    for (int i = 0; i < g_move_count; i++) {
        if (i > 0) strcat(history, " ");
        strcat(history, g_move_history[i]);
    }
    // Collect all matching entries
    const char *candidates[16];
    int num_candidates = 0;
    for (int i = 0; i < (int)BOOK_SIZE; i++) {
        if (strcmp(history, OPENING_BOOK[i].line) == 0) {
            if (num_candidates < 16)
                candidates[num_candidates++] = OPENING_BOOK[i].reply;
        }
    }
    if (num_candidates == 0) return 0;
    // Pick randomly among matches
    int choice = rand() % num_candidates;
    strcpy(out_move, candidates[choice]);
    return 1;
}

typedef struct {
    int from, to;
    char promo;
} Move;

typedef struct {
    char b[64];
    int white_to_move;
//castle
    int castle_wk;
    int castle_wq;
    int castle_bk;
    int castle_bq;

} Pos;

static int sq_index(const char *s) {
    int file = s[0] - 'a';
    int rank = s[1] - '1';
    return rank * 8 + file;
}

static void index_to_sq(int idx, char out[3]) {
    out[0] = (char) ('a' + (idx % 8));
    out[1] = (char) ('1' + (idx / 8));
    out[2] = 0;
}

static void pos_from_fen(Pos *p, const char *fen) {
    memset(p->b, '.', 64);
    p->white_to_move = 1;
//castling white side and black side
    p->castle_wk = 0;
    p->castle_wq = 0;
    p->castle_bk = 0;
    p->castle_bq = 0;

    char buf[256];
    strncpy(buf, fen, sizeof(buf)-1);
    buf[sizeof(buf) - 1] = 0;

    //char *save = NULL;
    char *placement = strtok(buf, " ");
    char *stm = strtok(NULL, " ");

    char *castling = strtok(NULL, " ");

    if (stm) p->white_to_move = (strcmp(stm, "w") == 0);
//casltiing
    if (castling) {
        p->castle_wk = (strchr(castling, 'K') != NULL);
        p->castle_wq = (strchr(castling, 'Q') != NULL);
        p->castle_bk = (strchr(castling, 'k') != NULL);
        p->castle_bq = (strchr(castling, 'q') != NULL);
    }
    
    int rank = 7, file = 0;
    for (size_t i = 0; placement && placement[i]; i++) {
        char c = placement[i];
        if (c == '/') {
            rank--;
            file = 0;
            continue;
        }
        if (isdigit((unsigned char) c)) {
            file += c - '0';
            continue;
        }
        int idx = rank * 8 + file;
        if (idx >= 0 && idx < 64) p->b[idx] = c;
        file++;
    }
}

static void pos_start(Pos *p) {
    pos_from_fen(p, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); //castling added
}

static int is_white_piece(char c) { return c >= 'A' && c <= 'Z'; }

static int is_square_attacked(const Pos *p, int sq, int by_white) {
    int r = sq / 8, f = sq % 8;

    // pawns
    if (by_white) {
        if (r > 0 && f > 0 && p->b[(r - 1) * 8 + (f - 1)] == 'P') return 1;
        if (r > 0 && f < 7 && p->b[(r - 1) * 8 + (f + 1)] == 'P') return 1;
    } else {
        if (r < 7 && f > 0 && p->b[(r + 1) * 8 + (f - 1)] == 'p') return 1;
        if (r < 7 && f < 7 && p->b[(r + 1) * 8 + (f + 1)] == 'p') return 1;
    }

    // knights
    static const int nd[8] = {-17, -15, -10, -6, 6, 10, 15, 17};
    for (int i = 0; i < 8; i++) {
        int to = sq + nd[i];
        if (to < 0 || to >= 64) continue;
        int tr = to / 8, tf = to % 8;
        int dr = tr - r;
        if (dr < 0) dr = -dr;
        int df = tf - f;
        if (df < 0) df = -df;
        if (!((dr == 1 && df == 2) || (dr == 2 && df == 1))) continue;
        char pc = p->b[to];
        if (by_white && pc == 'N') return 1;
        if (!by_white && pc == 'n') return 1;
    }

    // sliders
    static const int dirs[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };

    for (int di = 0; di < 8; di++) {
        int df = dirs[di][0], dr = dirs[di][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int idx = cr * 8 + cf;
            char pc = p->b[idx];
            if (pc != '.') {
                int pc_white = is_white_piece(pc);
                if (pc_white == by_white) {
                    char up = (char) toupper((unsigned char) pc);
                    int rook_dir = (di < 4);
                    int bishop_dir = (di >= 4);
                    if (up == 'Q') return 1;
                    if (rook_dir && up == 'R') return 1;
                    if (bishop_dir && up == 'B') return 1;
                    if (up == 'K' && (abs(cr - r) <= 1 && abs(cf - f) <= 1)) return 1;
                }
                break;
            }
            cr += dr;
            cf += df;
        }
    }

    // king adjacency (extra safety)
    for (int rr = r - 1; rr <= r + 1; rr++) {
        for (int ff = f - 1; ff <= f + 1; ff++) {
            if (rr < 0 || rr >= 8 || ff < 0 || ff >= 8) continue;
            if (rr == r && ff == f) continue;
            char pc = p->b[rr * 8 + ff];
            if (by_white && pc == 'K') return 1;
            if (!by_white && pc == 'k') return 1;
        }
    }

    return 0;
}

static int in_check(const Pos *p, int white_king) {
    char k = white_king ? 'K' : 'k';
    int ksq = -1;
    for (int i = 0; i < 64; i++) if (p->b[i] == k) {
        ksq = i;
        break;
    }
    if (ksq < 0) return 1;
    return is_square_attacked(p, ksq, !white_king);
}

static Pos make_move(const Pos *p, Move m) {
    Pos np = *p;
    char captured = p->b[m.to];
    char piece = np.b[m.from];
    np.b[m.from] = '.';
    char placed = piece;
    if (m.promo && (piece == 'P' || piece == 'p')) {
        placed = is_white_piece(piece)
                     ? (char) toupper((unsigned char) m.promo)
                     : (char) tolower((unsigned char) m.promo);
    }
    np.b[m.to] = placed;

    //rook move, castling
    if (piece == 'K' && m.from == 4 && m.to == 6) {
    np.b[7] = '.';
    np.b[5] = 'R';
} else if (piece == 'K' && m.from == 4 && m.to == 2) {
    np.b[0] = '.';
    np.b[3] = 'R';
} else if (piece == 'k' && m.from == 60 && m.to == 62) {
    np.b[63] = '.';
    np.b[61] = 'r';
} else if (piece == 'k' && m.from == 60 && m.to == 58) {
    np.b[56] = '.';
    np.b[59] = 'r';
}
    //castling for king
      if (piece == 'K') {
      np.castle_wk = 0;
      np.castle_wq = 0;
  } else if (piece == 'k') {
      np.castle_bk = 0;
      np.castle_bq = 0;
  }

  //castling for rook
  if (piece == 'R') {
      if (m.from == 0) np.castle_wq = 0;
      if (m.from == 7) np.castle_wk = 0;
  } else if (piece == 'r') {
      if (m.from == 56) np.castle_bq = 0;
      if (m.from == 63) np.castle_bk = 0;
  }
        //rook is captured 
    if (captured == 'R') {
    if (m.to == 0) np.castle_wq = 0;
    if (m.to == 7) np.castle_wk = 0;
} else if (captured == 'r') {
    if (m.to == 56) np.castle_bq = 0;
    if (m.to == 63) np.castle_bk = 0;
}
    
    np.white_to_move = !p->white_to_move;
    return np;
}

static void add_move(Move *moves, int *n, int from, int to, char promo) {
    moves[*n].from = from;
    moves[*n].to = to;
    moves[*n].promo = promo;
    (*n)++;
}

static void gen_pawn(const Pos *p, int from, int white, Move *moves, int *n) {
    int r = from / 8, f = from % 8;
    int dir = white ? 1 : -1;
    int start_r = white ? 1 : 6;
    int promo_r = white ? 7 : 0;

    int onesq = (r + dir) * 8 + f;

    char promos[4] = {'q', 'r', 'b', 'n'};

    // Moving forward
    if (r + dir >= 0 && r + dir < 8 && p->b[onesq] == '.') {
        if (r + dir == promo_r)
            for (int i = 0; i < 4; i++){
                add_move(moves, n, from, onesq, promos[i]);
            }
        else
            add_move(moves, n, from, onesq, 0);

        // Moving 2 squares
        if (r == start_r) {
            int twosq = (r + (2 * dir)) * 8 + f;
            if (p->b[twosq] == '.')
                add_move(moves, n, from, twosq, 0);
        }
    }

    // Captures
    for (int df = -1; df <= 1; df += 2) {
        int cf = f + df;
        int cr = r + dir;
        if (cf < 0 || cf >= 8 || cr < 0 || cr >= 8) continue;

        int to = cr * 8 + cf;
        char target = p->b[to];
        if (target != '.' && is_white_piece(target) != white) {
            if (cr == promo_r)
                for (int i = 0; i < 4; i++){
                add_move(moves, n, from, to, promos[i]);
            }
            else    
                add_move(moves, n, from, to, 0);
        }
    }
}

static void gen_knight(const Pos *p, int from, int white, Move *moves, int *n) {
    static const int nd[8] = {-17, -15, -10, -6, 6, 10, 15, 17};
    int r = from / 8, f = from % 8;

    for (int i = 0; i < 8; i++) {
        int to = from + nd[i];
        if (to < 0 || to >= 64) continue;
        int tr = to / 8, tf = to % 8;
        int dr = tr - r;
        if (dr < 0) dr = -dr;
        int df = tf - f;
        if (df < 0) df = -df;
        if (!((dr == 1 && df == 2) || (dr == 2 && df == 1))) continue;
        char pc = p->b[to];
        if (pc == '.' || is_white_piece(pc) != white) {
            add_move(moves, n, from, to, 0);
        }
    }
}

static void gen_queen(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int r = from / 8, f = from % 8;

    for (int di = 0; di <dcount; di++) {
        int df = dirs[di][0], dr = dirs[di][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int to = cr * 8 + cf;
            char pc = p->b[to];
            if (pc == '.') {
                add_move(moves, n, from, to, 0);
            } else {
                if (is_white_piece(pc) != white)
                    add_move(moves, n, from, to, 0);
                break;
            }
            cr += dr;
            cf += df;
        }
    }
}

static void gen_bishop(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int r = from / 8, f = from % 8;

    for (int di = 0; di <dcount; di++) {
        int df = dirs[di][0], dr = dirs[di][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int to = cr * 8 + cf;
            char pc = p->b[to];
            if (pc == '.') {
                add_move(moves, n, from, to, 0);
            } else {
                if (is_white_piece(pc) != white)
                    add_move(moves, n, from, to, 0);
                break;
            }
            cr += dr;
            cf += df;
        }
    }
}

static void gen_rook(const Pos *p, int from, int white, const int dirs[][2], int dcount, Move *moves, int *n) {
    int r = from / 8, f = from % 8;

    for (int di = 0; di <dcount; di++) {
        int df = dirs[di][0], dr = dirs[di][1];
        int cr = r + dr, cf = f + df;
        while (cr >= 0 && cr < 8 && cf >= 0 && cf < 8) {
            int to = cr * 8 + cf;
            char pc = p->b[to];
            if (pc == '.') {
                add_move(moves, n, from, to, 0);
            } else {
                if (is_white_piece(pc) != white)
                    add_move(moves, n, from, to, 0);
                break;
            }
            cr += dr;
            cf += df;
        }
    }
}

static void gen_king(const Pos *p, int from, int white, Move *moves, int *n) {
    int r = from / 8, f = from % 8;

    for (int rr = r - 1; rr <= r + 1; rr++) {
        for (int ff = f - 1; ff <= f + 1; ff++) {
            if (rr < 0 || rr >= 8 || ff < 0 || ff >= 8) continue;
            if (rr == r && ff == f) continue;
            int to = rr * 8 + ff;
            char pc = p->b[to];
            if (pc == '.' || is_white_piece(pc) != white) {
            add_move(moves, n, from, to, 0);
            }

        }
    }
if (white && from == 4 && p->b[4] == 'K' && !in_check(p, 1)) {
        if (p->castle_wk &&
            p->b[5] == '.' && p->b[6] == '.' && p->b[7] == 'R' &&
            !is_square_attacked(p, 5, 0) && !is_square_attacked(p, 6, 0)) {
            add_move(moves, n, from, 6, 0);
        }
        if (p->castle_wq &&
            p->b[1] == '.' && p->b[2] == '.' && p->b[3] == '.' && p->b[0] == 'R' &&
            !is_square_attacked(p, 3, 0) && !is_square_attacked(p, 2, 0)) {
            add_move(moves, n, from, 2, 0);
        }
    } else if (!white && from == 60 && p->b[60] == 'k' && !in_check(p, 0)) {
        if (p->castle_bk &&
            p->b[61] == '.' && p->b[62] == '.' && p->b[63] == 'r' &&
            !is_square_attacked(p, 61, 1) && !is_square_attacked(p, 62, 1)) {
            add_move(moves, n, from, 62, 0);
        }
        if (p->castle_bq &&
            p->b[57] == '.' && p->b[58] == '.' && p->b[59] == '.' && p->b[56] == 'r' &&
            !is_square_attacked(p, 59, 1) && !is_square_attacked(p, 58, 1)) {
            add_move(moves, n, from, 58, 0);
        }
    }
}

static int pseudo_legal_moves(const Pos *p, Move *moves) {
    int n = 0;
    int us_white = p->white_to_move;
    for (int i = 0; i < 64; i++) {
        char pc = p->b[i];
        if (pc == '.') continue;
        int white = is_white_piece(pc);
        if (white != us_white) continue;
        char up = (char) toupper((unsigned char) pc);
        if (up == 'P') gen_pawn(p, i, white, moves, &n);
        else if (up == 'N') gen_knight(p, i, white, moves, &n);
        else if (up == 'B') {
            static const int d[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
            gen_bishop(p, i, white, d, 4, moves, &n);
        } else if (up == 'R') {
            static const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            gen_rook(p, i, white, d, 4, moves, &n);
        } else if (up == 'Q') {
            static const int d[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            gen_queen(p, i, white, d, 8, moves, &n);
        } else if (up == 'K') gen_king(p, i, white, moves, &n);
    }
    return n;
}

static int legal_moves(const Pos *p, Move *out) {
    Move tmp[256];
    int pn = pseudo_legal_moves(p, tmp);
    int n = 0;
    for (int i = 0; i < pn; i++) {
        Pos np = make_move(p, tmp[i]);
        // after move, side who just moved is !np.white_to_move
        if (!in_check(&np, !np.white_to_move)) {
            out[n++] = tmp[i];
        }
    }
    return n;
}

static void apply_uci_move(Pos *p, const char *uci) {
    if (!uci || strlen(uci) < 4) return;
    Move m;
    m.from = sq_index(uci);
    m.to = sq_index(uci + 2);
    m.promo = (strlen(uci) >= 5) ? uci[4] : 0;
    Pos np = make_move(p, m);
    *p = np;
}

static void parse_position(Pos *p, const char *line) {
    // position startpos [moves ...]
    // position fen <6 fields> [moves ...]
    char buf[8192];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf) - 1] = 0;

    char *toks[1024];
    int nt = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t\r\n", &save); tok && nt < 1024; tok = strtok_r(NULL, " \t\r\n", &save)) {
        toks[nt++] = tok;
    }

    int i = 1;
    g_move_count = 0; // Reset move history
    g_pos_hash_count = 0; // Reset position hashes for repetition detection
    if (i < nt && strcmp(toks[i], "startpos") == 0) {
        pos_start(p);
        i++;
    } else if (i < nt && strcmp(toks[i], "fen") == 0) {
        i++;
        char fen[512] = {0};
        for (int k = 0; k < 6 && i < nt; k++, i++) {
            if (k)
                strcat(fen, " ");
            strcat(fen, toks[i]);
        }
        pos_from_fen(p, fen);
    }

    if (i < nt && strcmp(toks[i], "moves") == 0) {
        i++;
        for (; i < nt; i++) {
            // Record move in history for opening book
            if (g_move_count < 512) {
                strncpy(g_move_history[g_move_count], toks[i], 5);
                g_move_history[g_move_count][5] = 0;
                g_move_count++;
            }
            apply_uci_move(p, toks[i]);
            // Track all positions for repetition detection
            if (g_pos_hash_count < 1024) {
                g_pos_hashes[g_pos_hash_count++] = compute_pos_hash(p->b, p->white_to_move,
                    p->castle_wk | (p->castle_wq << 1) | (p->castle_bk << 2) | (p->castle_bq << 3));
            }
        }
    }
}

static void print_bestmove(Move m) {
    char a[3], b[3];
    index_to_sq(m.from, a);
    index_to_sq(m.to, b);
    if (m.promo) printf("bestmove %s%s%c\n", a, b, m.promo);
    else printf("bestmove %s%s\n", a, b);
    fflush(stdout);
}

// Standard relative piece values for material evaluation
#define PAWN_VALUE 100
#define KNIGHT_VALUE 300
#define BISHOP_VALUE 300
#define ROOK_VALUE 500
#define QUEEN_VALUE 900
#define KING_VALUE 10000 // Arbitrarily high value for the King

static const int PAWN_PST[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int KNIGHT_PST[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50
};

static const int GENERAL_PST[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5, 10, 20, 20, 10,  5,-10,
    -10,  5, 10, 20, 20, 10,  5,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -20,-10,-10,-10,-10,-10,-10,-20
};

/**
 * Enhanced evaluation: material + PST + pawn structure + bishop pair +
 * rook on open file + king safety.
 */
static int evaluate(const Pos *p) {
    int score = 0;
    int white_bishops = 0, black_bishops = 0;
    int white_pawns_on_file[8] = {0}, black_pawns_on_file[8] = {0};
    int white_pawn_present[8] = {0}, black_pawn_present[8] = {0};
    int wk_sq = -1, bk_sq = -1;

    // First pass: material + PST + count pieces
    for (int sq = 0; sq < 64; sq++) {
        char piece = p->b[sq];
        if (piece == '.') continue;
        int is_white = is_white_piece(piece);
        char up = (char)toupper((unsigned char)piece);
        int pst_idx = is_white ? sq : (56 - (sq / 8) * 8 + (sq % 8));
        int val = 0, pst = 0;
        int file = sq % 8;

        if (up == 'P') {
            val = PAWN_VALUE; pst = PAWN_PST[pst_idx];
            if (is_white) { white_pawns_on_file[file]++; white_pawn_present[file] = 1; }
            else          { black_pawns_on_file[file]++; black_pawn_present[file] = 1; }
        }
        else if (up == 'N') { val = KNIGHT_VALUE; pst = KNIGHT_PST[pst_idx]; }
        else if (up == 'B') {
            val = BISHOP_VALUE; pst = GENERAL_PST[pst_idx];
            if (is_white) white_bishops++; else black_bishops++;
        }
        else if (up == 'R') { val = ROOK_VALUE; pst = GENERAL_PST[pst_idx] / 2; }
        else if (up == 'Q') { val = QUEEN_VALUE; pst = GENERAL_PST[pst_idx]; }
        else if (up == 'K') {
            val = KING_VALUE; pst = -GENERAL_PST[pst_idx];
            if (is_white) wk_sq = sq; else bk_sq = sq;
        }

        if (is_white) score += val + pst;
        else          score -= (val + pst);
    }

    // Bishop pair bonus
    if (white_bishops >= 2) score += 50;
    if (black_bishops >= 2) score -= 50;

    // Pawn structure
    for (int f = 0; f < 8; f++) {
        // Doubled pawns penalty
        if (white_pawns_on_file[f] > 1) score -= 20 * (white_pawns_on_file[f] - 1);
        if (black_pawns_on_file[f] > 1) score += 20 * (black_pawns_on_file[f] - 1);
        // Isolated pawns penalty
        int wn = (f > 0 ? white_pawn_present[f-1] : 0) + (f < 7 ? white_pawn_present[f+1] : 0);
        int bn = (f > 0 ? black_pawn_present[f-1] : 0) + (f < 7 ? black_pawn_present[f+1] : 0);
        if (white_pawn_present[f] && wn == 0) score -= 15;
        if (black_pawn_present[f] && bn == 0) score += 15;
    }

    // Rook on open/semi-open file
    for (int sq = 0; sq < 64; sq++) {
        char pc = p->b[sq];
        if (pc == 'R' || pc == 'r') {
            int f = sq % 8;
            int is_w = (pc == 'R');
            int own_pawns = is_w ? white_pawns_on_file[f] : black_pawns_on_file[f];
            int opp_pawns = is_w ? black_pawns_on_file[f] : white_pawns_on_file[f];
            int bonus = 0;
            if (own_pawns == 0 && opp_pawns == 0) bonus = 25; // open file
            else if (own_pawns == 0) bonus = 15; // semi-open
            if (is_w) score += bonus; else score -= bonus;
        }
    }

    // King safety: bonus for pawn shield
    if (wk_sq >= 0) {
        int kf = wk_sq % 8, kr = wk_sq / 8;
        int shield = 0;
        for (int df = -1; df <= 1; df++) {
            int sf = kf + df;
            if (sf < 0 || sf > 7) continue;
            if (kr + 1 < 8 && p->b[(kr+1)*8+sf] == 'P') shield++;
        }
        score += shield * 10;
    }
    if (bk_sq >= 0) {
        int kf = bk_sq % 8, kr = bk_sq / 8;
        int shield = 0;
        for (int df = -1; df <= 1; df++) {
            int sf = kf + df;
            if (sf < 0 || sf > 7) continue;
            if (kr - 1 >= 0 && p->b[(kr-1)*8+sf] == 'p') shield++;
        }
        score -= shield * 10;
    }

    return score;
}

// Generate only legal captures and promotions (for quiescence search)
static int legal_captures(const Pos *p, Move *out) {
    Move tmp[256];
    int pn = pseudo_legal_moves(p, tmp);
    int n = 0;
    for (int i = 0; i < pn; i++) {
        if (p->b[tmp[i].to] == '.' && !tmp[i].promo) continue; // skip quiet moves
        Pos np = make_move(p, tmp[i]);
        if (!in_check(&np, !np.white_to_move)) {
            out[n++] = tmp[i];
        }
    }
    return n;
}

// ============================================================
// SEARCH ENGINE v2.0
// ============================================================
#define MAX_DEPTH 64
#define DEFAULT_DEPTH 7
#define MATE_SCORE 100000
#define INF_SCORE 999999
#define NULL_MOVE_R 2

// Killer moves: 2 per ply
static Move killers[MAX_DEPTH][2];

// History heuristic table [from][to]
static int history_table[64][64];

static int piece_value_for_ordering(char pc) {
    switch (toupper((unsigned char)pc)) {
        case 'P': return 100;  case 'N': return 300;
        case 'B': return 300;  case 'R': return 500;
        case 'Q': return 900;  case 'K': return 10000;
        default:  return 0;
    }
}

static int is_killer(Move m, int ply) {
    return (killers[ply][0].from == m.from && killers[ply][0].to == m.to) ||
           (killers[ply][1].from == m.from && killers[ply][1].to == m.to);
}

static void store_killer(Move m, int ply) {
    if (killers[ply][0].from != m.from || killers[ply][0].to != m.to) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
    }
}

static void order_moves(const Pos *p, Move *moves, int n, int ply) {
    int scores[256];
    for (int i = 0; i < n; i++) {
        char victim = p->b[moves[i].to];
        if (victim != '.') {
            scores[i] = 20000 + piece_value_for_ordering(victim) * 10
                        - piece_value_for_ordering(p->b[moves[i].from]);
        } else if (is_killer(moves[i], ply)) {
            scores[i] = 9000;
        } else {
            scores[i] = history_table[moves[i].from][moves[i].to];
        }
        if (moves[i].promo) scores[i] += 19000;
    }
    for (int i = 1; i < n; i++) {
        Move km = moves[i]; int ks = scores[i]; int j = i - 1;
        while (j >= 0 && scores[j] < ks) {
            moves[j+1] = moves[j]; scores[j+1] = scores[j]; j--;
        }
        moves[j+1] = km; scores[j+1] = ks;
    }
}

/**
 * Quiescence Search: extends search at leaf nodes by evaluating
 * all captures until the position is "quiet". Eliminates the
 * horizon effect (e.g. not seeing a recapture).
 */
static int quiescence(Pos *p, int alpha, int beta) {
    int stand_pat = evaluate(p);
    int maximizing = p->white_to_move;

    if (maximizing) {
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
        Move caps[256];
        int n = legal_captures(p, caps);
        order_moves(p, caps, n, 0);
        for (int i = 0; i < n; i++) {
            Pos next = make_move(p, caps[i]);
            int score = quiescence(&next, alpha, beta);
            if (score > alpha) alpha = score;
            if (alpha >= beta) return beta;
        }
        return alpha;
    } else {
        if (stand_pat <= alpha) return alpha;
        if (stand_pat < beta) beta = stand_pat;
        Move caps[256];
        int n = legal_captures(p, caps);
        order_moves(p, caps, n, 0);
        for (int i = 0; i < n; i++) {
            Pos next = make_move(p, caps[i]);
            int score = quiescence(&next, alpha, beta);
            if (score < beta) beta = score;
            if (alpha >= beta) return alpha;
        }
        return beta;
    }
}

/**
 * Alpha-Beta with Null Move Pruning, Killer Moves, LMR, and
 * Quiescence Search at leaf nodes.
 */
static int alpha_beta(Pos *p, int depth, int alpha, int beta,
                      int ply, int do_null) {
    if (depth <= 0) return quiescence(p, alpha, beta);

    int maximizing = p->white_to_move;

    // Repetition detection inside search: treat repeated positions as draws
    unsigned long long ph = compute_pos_hash(p->b, p->white_to_move,
        p->castle_wk | (p->castle_wq << 1) | (p->castle_bk << 2) | (p->castle_bq << 3));
    if (ply > 0 && is_repetition(ph, 1)) return 0; // draw

    Move moves[256];
    int n = legal_moves(p, moves);

    if (n == 0) {
        if (in_check(p, p->white_to_move))
            return maximizing ? -MATE_SCORE + ply : MATE_SCORE - ply;
        return 0; // stalemate
    }

    // Null Move Pruning: skip our turn at reduced depth
    if (do_null && depth >= 3 && !in_check(p, p->white_to_move)) {
        Pos null_pos = *p;
        null_pos.white_to_move = !null_pos.white_to_move;
        int null_score = alpha_beta(&null_pos, depth - 1 - NULL_MOVE_R,
                                    alpha, beta, ply + 1, 0);
        if (maximizing && null_score >= beta) return beta;
        if (!maximizing && null_score <= alpha) return alpha;
    }

    order_moves(p, moves, n, ply);

    if (maximizing) {
        int best = -INF_SCORE;
        for (int i = 0; i < n; i++) {
            int rdepth = depth - 1;
            // LMR: reduce depth for late quiet moves
            if (i >= 4 && depth >= 3 && p->b[moves[i].to] == '.' && !moves[i].promo)
                rdepth--;

            Pos next = make_move(p, moves[i]);
            int score = alpha_beta(&next, rdepth, alpha, beta, ply + 1, 1);

            // Re-search at full depth if LMR found something good
            if (rdepth < depth - 1 && score > alpha)
                score = alpha_beta(&next, depth - 1, alpha, beta, ply + 1, 1);

            if (score > best) best = score;
            if (score > alpha) alpha = score;
            if (alpha >= beta) {
                if (p->b[moves[i].to] == '.') {
                    store_killer(moves[i], ply);
                    history_table[moves[i].from][moves[i].to] += depth * depth;
                }
                break;
            }
        }
        return best;
    } else {
        int best = INF_SCORE;
        for (int i = 0; i < n; i++) {
            int rdepth = depth - 1;
            if (i >= 4 && depth >= 3 && p->b[moves[i].to] == '.' && !moves[i].promo)
                rdepth--;

            Pos next = make_move(p, moves[i]);
            int score = alpha_beta(&next, rdepth, alpha, beta, ply + 1, 1);

            if (rdepth < depth - 1 && score < beta)
                score = alpha_beta(&next, depth - 1, alpha, beta, ply + 1, 1);

            if (score < best) best = score;
            if (score < beta) beta = score;
            if (alpha >= beta) {
                if (p->b[moves[i].to] == '.') {
                    store_killer(moves[i], ply);
                    history_table[moves[i].from][moves[i].to] += depth * depth;
                }
                break;
            }
        }
        return best;
    }
}

/**
 * Iterative Deepening: search depth 1, then 2, ... up to DEFAULT_DEPTH.
 * Best move from previous depth improves ordering at the next depth.
 */
static Move find_best_move(Pos *p, Move *moves, int num_moves) {
    // Clear killer and history tables
    memset(killers, 0, sizeof(killers));
    memset(history_table, 0, sizeof(history_table));

    Move best_move = moves[0];

    for (int depth = 1; depth <= DEFAULT_DEPTH; depth++) {
        order_moves(p, moves, num_moves, 0);

        // Put previous iteration's best move first
        if (depth > 1) {
            for (int i = 1; i < num_moves; i++) {
                if (moves[i].from == best_move.from && moves[i].to == best_move.to
                    && moves[i].promo == best_move.promo) {
                    Move tmp = moves[0]; moves[0] = moves[i]; moves[i] = tmp;
                    break;
                }
            }
        }

        int alpha = -INF_SCORE, beta = INF_SCORE;
        int best_idx = 0;

        if (p->white_to_move) {
            int best_score = -INF_SCORE;
            for (int i = 0; i < num_moves; i++) {
                Pos next = make_move(p, moves[i]);

                // Penalize moves that repeat a known position
                unsigned long long next_hash = compute_pos_hash(next.b, next.white_to_move,
                    next.castle_wk | (next.castle_wq << 1) | (next.castle_bk << 2) | (next.castle_bq << 3));
                if (is_repetition(next_hash, 1)) {
                    // Treat as slightly worse than a draw to break loops
                    int score = -10;
                    if (score > best_score) { best_score = score; best_idx = i; }
                    continue;
                }

                int score = alpha_beta(&next, depth - 1, alpha, beta, 1, 1);
                if (score > best_score) { best_score = score; best_idx = i; }
                if (score > alpha) alpha = score;
            }
        } else {
            int best_score = INF_SCORE;
            for (int i = 0; i < num_moves; i++) {
                Pos next = make_move(p, moves[i]);

                unsigned long long next_hash = compute_pos_hash(next.b, next.white_to_move,
                    next.castle_wk | (next.castle_wq << 1) | (next.castle_bk << 2) | (next.castle_bq << 3));
                if (is_repetition(next_hash, 1)) {
                    int score = 10;
                    if (score < best_score) { best_score = score; best_idx = i; }
                    continue;
                }

                int score = alpha_beta(&next, depth - 1, alpha, beta, 1, 1);
                if (score < best_score) { best_score = score; best_idx = i; }
                if (score < beta) beta = score;
            }
        }

        best_move = moves[best_idx];
    }

    return best_move;
}


int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    srand(time(NULL));

    Pos pos;
    pos_start(&pos);

    char line[8192];
    while (fgets(line, sizeof(line), stdin)) {
        // trim
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;

        if (strcmp(line, "uci") == 0) {
            printf("id name team_c %s\n", ENGINE_VERSION);
            printf("id author team_c_bryan\n");
            printf("uciok\n");
            fflush(stdout);
        } else if (strcmp(line, "isready") == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(line, "ucinewgame") == 0) {
            pos_start(&pos);
            g_move_count = 0;
            g_pos_hash_count = 0; // Reset position history for new game
        } else if (strncmp(line, "position", 8) == 0) {
            parse_position(&pos, line);
        } else if (strncmp(line, "go", 2) == 0) {
            // Check opening book first
            char book_move[8];
            if (book_lookup(book_move)) {
                // Track book move position for repetition detection
                Pos book_pos = pos;
                apply_uci_move(&book_pos, book_move);
                if (g_pos_hash_count < 1024) {
                    g_pos_hashes[g_pos_hash_count++] = compute_pos_hash(book_pos.b, book_pos.white_to_move,
                        book_pos.castle_wk | (book_pos.castle_wq << 1) | (book_pos.castle_bk << 2) | (book_pos.castle_bq << 3));
                }
                printf("bestmove %s\n", book_move);
                fflush(stdout);
            } else {
                Move ms[256];
                int n = legal_moves(&pos, ms);
                if (n <= 0) {
                    printf("bestmove 0000\n");
                    fflush(stdout);
                } else {
                    Move best_calculated_move = find_best_move(&pos, ms, n);
                    print_bestmove(best_calculated_move);
                }
            }
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }
    return 0;
}
