#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

// Minimal UCI engine: first legal move.
// No castling, no en-passant; promotions -> queen only.
//has included caslting

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
    int twosq = (r + 2 * dir) * 8 + f;

    char promos[4] = {'q', 'r', 'b', 'n'};

    // Moving forward
    if (r + dir >= 0 && r + dir < 8 && p->b[onesq] == '.') {
        if (r == promo_r)
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
            if (r == promo_r)
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
    char buf[1024];
    strncpy(buf, line, sizeof(buf)-1);
    buf[sizeof(buf) - 1] = 0;

    char *toks[128];
    int nt = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t\r\n", &save); tok && nt < 128; tok = strtok_r(NULL, " \t\r\n", &save)) {
        toks[nt++] = tok;
    }

    int i = 1;
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
        for (; i < nt; i++) apply_uci_move(p, toks[i]);
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
 * Evaluates the board state based strictly on standard material weights.
 * Returns a positive score if White is ahead, and a negative score if Black is ahead.
 */
static int evaluate(const Pos *p) {
    int material_score = 0;
    
    // Iterate through all 64 squares on the board
    for (int square_index = 0; square_index < 64; square_index++) {
        char piece = p->b[square_index];
        if (piece == '.') continue; // Empty square
        
        int piece_value = 0;
        int pst_val = 0;
        char normalized_piece = (char)toupper((unsigned char)piece);
        int is_white = is_white_piece(piece);
        
        // Find corresponding square index for PST (flip for black)
        int pst_index = is_white ? square_index : (56 - (square_index / 8) * 8 + (square_index % 8));

        // Assign value based on piece type
        if (normalized_piece == 'P') { piece_value = PAWN_VALUE; pst_val = PAWN_PST[pst_index]; }
        else if (normalized_piece == 'N') { piece_value = KNIGHT_VALUE; pst_val = KNIGHT_PST[pst_index]; }
        else if (normalized_piece == 'B') { piece_value = BISHOP_VALUE; pst_val = GENERAL_PST[pst_index]; }
        else if (normalized_piece == 'R') { piece_value = ROOK_VALUE; pst_val = GENERAL_PST[pst_index] / 2; }
        else if (normalized_piece == 'Q') { piece_value = QUEEN_VALUE; pst_val = GENERAL_PST[pst_index]; }
        else if (normalized_piece == 'K') { piece_value = KING_VALUE; pst_val = -GENERAL_PST[pst_index]; } // keep king slightly safe
        
        // Add to White's score, subtract for Black's score
        if (is_white) {
            material_score += piece_value + pst_val;
        } else {
            material_score -= (piece_value + pst_val);
        }
    }
    
    return material_score;
}

// Search depth for Minimax with Alpha-Beta pruning.
// Depth 5 exceeds the Java engine's depth of 4, and C's speed makes it feasible.
#define SEARCH_DEPTH 5

/**
 * Returns a rough piece value used for MVV-LVA move ordering.
 */
static int piece_value_for_ordering(char pc) {
    switch (toupper((unsigned char)pc)) {
        case 'P': return 100;
        case 'N': return 300;
        case 'B': return 300;
        case 'R': return 500;
        case 'Q': return 900;
        case 'K': return 10000;
        default:  return 0;
    }
}

/**
 * Simple move ordering: sort captures before quiet moves using MVV-LVA
 * (Most Valuable Victim – Least Valuable Aggressor).
 * This dramatically improves alpha-beta pruning efficiency.
 */
static void order_moves(const Pos *p, Move *moves, int n) {
    int scores[256];
    for (int i = 0; i < n; i++) {
        char victim  = p->b[moves[i].to];
        char attacker = p->b[moves[i].from];
        if (victim != '.') {
            // MVV-LVA: prioritise capturing high-value pieces with low-value pieces
            scores[i] = 10000 + piece_value_for_ordering(victim) * 10
                        - piece_value_for_ordering(attacker);
        } else {
            scores[i] = 0;
        }
        // Bonus for promotions
        if (moves[i].promo) scores[i] += 9000;
    }
    // Simple insertion sort (fast for small N)
    for (int i = 1; i < n; i++) {
        Move key_m = moves[i];
        int  key_s = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j] < key_s) {
            moves[j + 1] = moves[j];
            scores[j + 1] = scores[j];
            j--;
        }
        moves[j + 1] = key_m;
        scores[j + 1] = key_s;
    }
}

/**
 * Recursive Alpha-Beta search.
 * @param p          current position
 * @param depth      remaining depth to search
 * @param alpha      best score White can guarantee (lower bound)
 * @param beta       best score Black can guarantee (upper bound)
 * @param maximizing 1 if it's the maximizing player's turn (White)
 * @return evaluation score of the position
 */
static int alpha_beta(Pos *p, int depth, int alpha, int beta, int maximizing) {
    // Base case: evaluate leaf node
    if (depth == 0) {
        return evaluate(p);
    }

    Move moves[256];
    int n = legal_moves(p, moves);

    // No legal moves: checkmate or stalemate
    if (n == 0) {
        // If the side to move is in check, it's checkmate
        if (in_check(p, p->white_to_move)) {
            // Return a large penalty for the side that got checkmated.
            // Add depth bonus so the engine prefers faster checkmates.
            return maximizing ? -100000 + (SEARCH_DEPTH - depth)
                              :  100000 - (SEARCH_DEPTH - depth);
        }
        // Stalemate = draw = 0
        return 0;
    }

    // Order moves for better pruning
    order_moves(p, moves, n);

    if (maximizing) {
        int max_eval = -999999;
        for (int i = 0; i < n; i++) {
            Pos next = make_move(p, moves[i]);
            int eval = alpha_beta(&next, depth - 1, alpha, beta, 0);
            if (eval > max_eval) max_eval = eval;
            if (eval > alpha)    alpha = eval;
            if (beta <= alpha)   break; // Beta cutoff
        }
        return max_eval;
    } else {
        int min_eval = 999999;
        for (int i = 0; i < n; i++) {
            Pos next = make_move(p, moves[i]);
            int eval = alpha_beta(&next, depth - 1, alpha, beta, 1);
            if (eval < min_eval) min_eval = eval;
            if (eval < beta)     beta = eval;
            if (beta <= alpha)   break; // Alpha cutoff
        }
        return min_eval;
    }
}

/**
 * Minimax with Alpha-Beta Pruning (Depth 5).
 *
 * Looks 5 half-moves ahead, simulating "if I move here, then my opponent
 * moves there, then I respond..." and picks the move that leads to the best
 * guaranteed outcome assuming the opponent also plays optimally.
 *
 * Move ordering (MVV-LVA for captures, promotions first) improves pruning
 * efficiency by causing more cutoffs earlier in the search.
 */
static Move find_best_move(Pos *p, Move *moves, int num_moves) {
    // Order moves: captures / promotions first for better alpha-beta cutoff
    order_moves(p, moves, num_moves);

    int best_move_index = 0;
    int alpha = -999999;
    int beta  =  999999;

    if (p->white_to_move) {
        int best_score = -999999;
        for (int i = 0; i < num_moves; i++) {
            Pos next = make_move(p, moves[i]);
            int score = alpha_beta(&next, SEARCH_DEPTH - 1, alpha, beta, 0);
            if (score > best_score) {
                best_score = score;
                best_move_index = i;
            }
            if (best_score > alpha) alpha = best_score;
        }
    } else {
        int best_score = 999999;
        for (int i = 0; i < num_moves; i++) {
            Pos next = make_move(p, moves[i]);
            int score = alpha_beta(&next, SEARCH_DEPTH - 1, alpha, beta, 1);
            if (score < best_score) {
                best_score = score;
                best_move_index = i;
            }
            if (best_score < beta) beta = best_score;
        }
    }

    return moves[best_move_index];
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdin, NULL, _IONBF, 0);
    srand(time(NULL));

    Pos pos;
    pos_start(&pos);

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        // trim
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (!len) continue;

        if (strcmp(line, "uci") == 0) {
            printf("id name team_c\n");
            printf("id author team_c_bryan\n");
            printf("uciok\n");
            fflush(stdout);
        } else if (strcmp(line, "isready") == 0) {
            printf("readyok\n");
            fflush(stdout);
        } else if (strcmp(line, "ucinewgame") == 0) {
            pos_start(&pos);
        } else if (strncmp(line, "position", 8) == 0) {
            parse_position(&pos, line);
        } else if (strncmp(line, "go", 2) == 0) {
            Move ms[256];
            int n = legal_moves(&pos, ms);
            if (n <= 0) {
                printf("bestmove 0000\n");
                fflush(stdout);
            } else {
                Move best_calculated_move = find_best_move(&pos, ms, n);
                print_bestmove(best_calculated_move);
            }
        } else if (strcmp(line, "quit") == 0) {
            break;
        }
    }
    return 0;
}
