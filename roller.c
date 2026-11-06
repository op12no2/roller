/*
 * roller - a chess engine with no search.
 *
 * For every legal move in the root position, play random games to the end
 * and score them win 2, draw 1, loss 0. The move with the most points per
 * game is played. Monte Carlo tree search without the tree. The random
 * games pick uniformly among legal moves, except that a side which can
 * capture does so CapturePercent of the time, taking the most valuable
 * victim (CaptureBest), and a side which can give a direct check does so
 * CheckPercent of the time. Defaults 50, on, 50: together about 500 Elo
 * over pure random in self-play, because material and king safety only
 * matter in a world where hanging pieces get taken and exposed kings get
 * checked. The only other knowledge is the rules of
 * chess: checkmate, stalemate, the fifty-move rule and dead positions end a
 * game; games longer than ClipPlies plies are cut off and scored as draws
 * (or by material with ClipScore=Material, which tested as no better).
 * Repetition is not detected inside playouts (the fifty-move rule and the
 * clip take care of shuffling).
 *
 * The reported score is plain material, side to move, 1/3/3/5/9. Depth is
 * the number of games played per root move, seldepth the longest game,
 * nodes the number of positions played through.
 *
 * Options: Threads, ClipPlies, Policy (Uniform: every legal move equally
 * likely; Piece: pick one of your pieces at random, then one of its moves),
 * ClipScore, CapturePercent, CaptureBest, CheckPercent, ReplyMin (score a
 * move by its worst opponent reply, from the games already played; tested
 * worse, off by default),
 * MoveOverhead, TimeDivisor. Console: d, eval, perft N, bench [seconds].
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#define NAME "Roller 1.2"
#define AUTHOR "Claude Fable 5.1"

/* ---------------------------------------------------------------- board */

enum { EMPTY = 0, P = 1, N, B, R, Q, K };
enum { WHITE = 0, BLACK = 1 };
#define COL(p) ((p) >> 3)
#define TYPE(p) ((p) & 7)
#define MK(c, t) (((c) << 3) | (t))
#define ONB(s) (!((s) & 0x88))
#define EP_NONE 0x08           /* an off-board square that is still a valid index */
#define MAXMOVES 256

typedef struct {
  uint8_t sq[128];             /* 0x88 board, 0 = empty, else MK(colour, type) */
  uint8_t idx[128];            /* index of the piece on a square in list[] */
  uint8_t list[2][16];         /* squares of each side's pieces, king first */
  uint8_t n[2];
  uint8_t side, castle, ep, hm;
  uint8_t pawns[2], minors[2], majors[2];
  int16_t mat[2];
} Board;

typedef uint32_t Move;         /* from | to << 7 | promo << 14 */
#define MFROM(m) ((m) & 127)
#define MTO(m) (((m) >> 7) & 127)
#define MPROMO(m) (((m) >> 14) & 7)
#define MOVE(f, t, p) ((Move)((f) | ((t) << 7) | ((p) << 14)))

static const int val[7] = { 0, 100, 300, 300, 500, 900, 0 };
static const int n_off[8] = { 33, 31, 18, 14, -33, -31, -18, -14 };
static const int k_off[8] = { 17, 16, 15, 1, -1, -15, -16, -17 };
static const int b_off[4] = { 17, 15, -17, -15 };
static const int r_off[4] = { 16, 1, -16, -1 };
static uint8_t castle_mask[128];

static void add_piece(Board *b, int s, int p) {
  int c = COL(p), t = TYPE(p);
  b->sq[s] = p;
  if (t == K) {                /* king lives at index 0 */
    if (b->n[c]) { int f = b->list[c][0]; b->list[c][b->n[c]] = f; b->idx[f] = b->n[c]; }
    b->list[c][0] = s; b->idx[s] = 0; b->n[c]++;
  } else {
    b->idx[s] = b->n[c]; b->list[c][b->n[c]++] = s;
  }
  b->mat[c] += val[t];
  if (t == P) b->pawns[c]++;
  else if (t == N || t == B) b->minors[c]++;
  else if (t == R || t == Q) b->majors[c]++;
}

static inline void remove_piece(Board *b, int s) {
  int p = b->sq[s], c = COL(p), t = TYPE(p);
  int k = b->idx[s], last = b->list[c][--b->n[c]];
  b->list[c][k] = last; b->idx[last] = k;
  b->sq[s] = 0;
  b->mat[c] -= val[t];
  if (t == P) b->pawns[c]--;
  else if (t == N || t == B) b->minors[c]--;
  else b->majors[c]--;
}

static int attacked(const Board *b, int s, int by) {
  int t, i;
  const uint8_t *sq = b->sq;
  if (by == WHITE) {
    if (ONB(s - 15) && sq[s - 15] == MK(WHITE, P)) return 1;
    if (ONB(s - 17) && sq[s - 17] == MK(WHITE, P)) return 1;
  } else {
    if (ONB(s + 15) && sq[s + 15] == MK(BLACK, P)) return 1;
    if (ONB(s + 17) && sq[s + 17] == MK(BLACK, P)) return 1;
  }
  for (i = 0; i < 8; i++) { t = s + n_off[i]; if (ONB(t) && sq[t] == MK(by, N)) return 1; }
  for (i = 0; i < 8; i++) { t = s + k_off[i]; if (ONB(t) && sq[t] == MK(by, K)) return 1; }
  for (i = 0; i < 4; i++) {
    for (t = s + b_off[i]; ONB(t); t += b_off[i])
      if (sq[t]) { if (sq[t] == MK(by, B) || sq[t] == MK(by, Q)) return 1; break; }
    for (t = s + r_off[i]; ONB(t); t += r_off[i])
      if (sq[t]) { if (sq[t] == MK(by, R) || sq[t] == MK(by, Q)) return 1; break; }
  }
  return 0;
}

#define ADD(f, t, p) (m[n++] = MOVE(f, t, p))

/* Pseudo-legal moves of the piece on s. Castling is generated fully legal
   except for the destination square, which the post-make check covers. */
static int gen_piece(const Board *b, int s, Move *m) {
  int n = 0, i, t, p = b->sq[s], c = COL(p), o = c ^ 1;
  const uint8_t *sq = b->sq;
  switch (TYPE(p)) {
  case P: {
    int fwd = c == WHITE ? 16 : -16, rank = s >> 4;
    int promo = c == WHITE ? rank == 6 : rank == 1;
    int start = c == WHITE ? rank == 1 : rank == 6;
    t = s + fwd;
    if (!sq[t]) {
      if (promo) { ADD(s, t, Q); ADD(s, t, R); ADD(s, t, B); ADD(s, t, N); }
      else { ADD(s, t, 0); if (start && !sq[t + fwd]) ADD(s, t + fwd, 0); }
    }
    for (i = -1; i <= 1; i += 2) {
      t = s + fwd + i;
      if (!ONB(t)) continue;
      if (sq[t]) {
        if (COL(sq[t]) != c) {
          if (promo) { ADD(s, t, Q); ADD(s, t, R); ADD(s, t, B); ADD(s, t, N); }
          else ADD(s, t, 0);
        }
      } else if (t == b->ep) ADD(s, t, 0);
    }
    break;
  }
  case N:
    for (i = 0; i < 8; i++) { t = s + n_off[i]; if (ONB(t) && (!sq[t] || COL(sq[t]) != c)) ADD(s, t, 0); }
    break;
  case K:
    for (i = 0; i < 8; i++) { t = s + k_off[i]; if (ONB(t) && (!sq[t] || COL(sq[t]) != c)) ADD(s, t, 0); }
    if (b->castle) {
      int ks = c == WHITE ? 1 : 4, qs = c == WHITE ? 2 : 8;
      if ((b->castle & ks) && !sq[s + 1] && !sq[s + 2] && !attacked(b, s, o) && !attacked(b, s + 1, o)) ADD(s, s + 2, 0);
      if ((b->castle & qs) && !sq[s - 1] && !sq[s - 2] && !sq[s - 3] && !attacked(b, s, o) && !attacked(b, s - 1, o)) ADD(s, s - 2, 0);
    }
    break;
  case B:
    for (i = 0; i < 4; i++) for (t = s + b_off[i]; ONB(t); t += b_off[i]) {
      if (!sq[t]) ADD(s, t, 0); else { if (COL(sq[t]) != c) ADD(s, t, 0); break; } }
    break;
  case R:
    for (i = 0; i < 4; i++) for (t = s + r_off[i]; ONB(t); t += r_off[i]) {
      if (!sq[t]) ADD(s, t, 0); else { if (COL(sq[t]) != c) ADD(s, t, 0); break; } }
    break;
  case Q:
    for (i = 0; i < 8; i++) for (t = s + k_off[i]; ONB(t); t += k_off[i]) {
      if (!sq[t]) ADD(s, t, 0); else { if (COL(sq[t]) != c) ADD(s, t, 0); break; } }
    break;
  }
  return n;
}

static int gen_all(const Board *b, Move *m) {
  int n = 0, i, c = b->side;
  for (i = 0; i < b->n[c]; i++) n += gen_piece(b, b->list[c][i], m + n);
  return n;
}

static void make(Board *b, Move mv) {
  int from = MFROM(mv), to = MTO(mv), promo = MPROMO(mv);
  int p = b->sq[from], c = COL(p), t = TYPE(p), k;
  b->hm++;
  if (t == P) {
    b->hm = 0;
    if (to == b->ep) remove_piece(b, to + (c == WHITE ? -16 : 16));
  }
  if (b->sq[to]) { b->hm = 0; remove_piece(b, to); }
  k = b->idx[from]; b->list[c][k] = to; b->idx[to] = k;
  b->sq[to] = p; b->sq[from] = 0;
  b->ep = EP_NONE;
  if (t == P) {
    if (promo) {
      b->sq[to] = MK(c, promo); b->pawns[c]--; b->mat[c] += val[promo] - val[P];
      if (promo == N || promo == B) b->minors[c]++; else b->majors[c]++;
    } else if (to - from == 32 || from - to == 32) b->ep = (from + to) >> 1;
  } else if (t == K) {
    int rf = -1, rt = 0;
    if (to - from == 2) { rf = from + 3; rt = from + 1; }
    else if (from - to == 2) { rf = from - 4; rt = from - 1; }
    if (rf >= 0) { k = b->idx[rf]; b->list[c][k] = rt; b->idx[rt] = k; b->sq[rt] = b->sq[rf]; b->sq[rf] = 0; }
  }
  b->castle &= castle_mask[from] & castle_mask[to];
  b->side = c ^ 1;
}

/* did the side that just moved leave its king in check? */
static inline int illegal(const Board *b) {
  int c = b->side ^ 1;
  return attacked(b, b->list[c][0], b->side);
}

static inline int in_check(const Board *b) {
  return attacked(b, b->list[b->side][0], b->side ^ 1);
}

static int gen_legal(const Board *b, Move *m) {
  Move all[MAXMOVES];
  int n = gen_all(b, all), k = 0, i;
  for (i = 0; i < n; i++) { Board t = *b; make(&t, all[i]); if (!illegal(&t)) m[k++] = all[i]; }
  return k;
}

/* insufficient material: no pawns or majors and at most one minor, or one
   bishop each on the same colour */
static int dead(const Board *b) {
  int m;
  if (b->pawns[0] | b->pawns[1] | b->majors[0] | b->majors[1]) return 0;
  m = b->minors[0] + b->minors[1];
  if (m <= 1) return 1;
  if (m == 2 && b->minors[0] == 1 && b->minors[1] == 1) {
    int s0 = b->list[0][1], s1 = b->list[1][1];
    if (TYPE(b->sq[s0]) == B && TYPE(b->sq[s1]) == B &&
        (((s0 >> 4) + (s0 & 7)) & 1) == (((s1 >> 4) + (s1 & 7)) & 1)) return 1;
  }
  return 0;
}

/* ------------------------------------------------------- fen and strings */

static const char piece_chars[] = ".PNBRQK..pnbrqk";

static void set_fen(Board *b, const char *fen) {
  int s = 0x70, i;
  const char *p;
  memset(b, 0, sizeof *b);
  b->ep = EP_NONE;
  for (p = fen; *p && *p != ' '; p++) {
    if (*p == '/') { s -= 24; continue; }
    if (*p >= '1' && *p <= '8') { s += *p - '0'; continue; }
    const char *q = strchr(piece_chars, *p);
    if (q && ONB(s)) add_piece(b, s, (int)(q - piece_chars));
    s++;
  }
  while (*p == ' ') p++;
  b->side = *p == 'b' ? BLACK : WHITE;
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  for (; *p && *p != ' '; p++) {
    if (*p == 'K' && b->sq[0x04] == MK(WHITE, K) && b->sq[0x07] == MK(WHITE, R)) b->castle |= 1;
    if (*p == 'Q' && b->sq[0x04] == MK(WHITE, K) && b->sq[0x00] == MK(WHITE, R)) b->castle |= 2;
    if (*p == 'k' && b->sq[0x74] == MK(BLACK, K) && b->sq[0x77] == MK(BLACK, R)) b->castle |= 4;
    if (*p == 'q' && b->sq[0x74] == MK(BLACK, K) && b->sq[0x70] == MK(BLACK, R)) b->castle |= 8;
  }
  while (*p == ' ') p++;
  if (*p >= 'a' && *p <= 'h' && p[1] >= '1' && p[1] <= '8') b->ep = (p[1] - '1') * 16 + (*p - 'a');
  while (*p && *p != ' ') p++;
  while (*p == ' ') p++;
  i = atoi(p); b->hm = i < 0 ? 0 : i > 255 ? 255 : i;
  for (i = 0; i < 2; i++) if (!b->n[i]) add_piece(b, i ? 0x74 : 0x04, MK(i, K)); /* never run without kings */
}

static const char *move_str(Move m) {
  static char buf[4][8];
  static int k;
  char *s = buf[k++ & 3];
  int f = MFROM(m), t = MTO(m), p = MPROMO(m);
  sprintf(s, "%c%c%c%c%c", 'a' + (f & 7), '1' + (f >> 4), 'a' + (t & 7), '1' + (t >> 4), p ? "  nbrq"[p] : 0);
  return s;
}

static void print_board(const Board *b) {
  int r, f;
  for (r = 7; r >= 0; r--) {
    printf("  ");
    for (f = 0; f < 8; f++) printf("%c ", piece_chars[b->sq[r * 16 + f]]);
    printf("\n");
  }
  printf("  %s to move, castle %c%c%c%c, ep %s, halfmove %d, material %+d\n",
         b->side ? "black" : "white", b->castle & 1 ? 'K' : '-', b->castle & 2 ? 'Q' : '-',
         b->castle & 4 ? 'k' : '-', b->castle & 8 ? 'q' : '-',
         b->ep == EP_NONE ? "-" : move_str(MOVE(b->ep, b->ep, 0)) + 2, b->hm, b->mat[b->side] - b->mat[b->side ^ 1]);
}

static int material(const Board *b) { return b->mat[b->side] - b->mat[b->side ^ 1]; }

/* ------------------------------------------------------------- playouts */

enum { POL_UNIFORM = 0, POL_PIECE };
enum { END_MATE = 0, END_STALEMATE, END_FIFTY, END_DEAD, END_CLIP, END_COUNT };

static int opt_threads = 1, opt_clip = 300, opt_policy = POL_UNIFORM, opt_overhead = 50, opt_divisor = 25;
static int opt_clipmat = 0;      /* score clipped games by material instead of as draws */
static int opt_capture = 50;     /* percent chance of playing a capture when one exists (Uniform policy) */
static int opt_replymin = 0;     /* score a root move by its worst opponent reply, using the games already played */
static int opt_check = 50;       /* percent chance of playing a checking move when one exists (direct checks only) */
static int opt_mvv = 1;          /* when capturing, only the most valuable victims are considered */

typedef struct {
  uint64_t rng;
  int id;
  _Atomic uint32_t cnt[MAXMOVES], pts[MAXMOVES];
  _Atomic uint64_t games, plies, ends[END_COUNT];
  _Atomic uint32_t maxlen;
  _Atomic uint32_t *rcnt, *rpts;   /* [root move * MAXMOVES + reply], ReplyMin only */
  uint8_t rj[MAXMOVES];            /* next reply to try per root move */
  pthread_t th;
  char pad[64];
} Thread;

static inline uint64_t rnd(uint64_t *s) {   /* wyrand */
  *s += 0xa0761d6478bd642fULL;
  __uint128_t t = (__uint128_t)*s * (*s ^ 0xe7037ed1a0b428dbULL);
  return (uint64_t)(t >> 64) ^ (uint64_t)t;
}
static inline int rnd_below(uint64_t *s, int n) { return (int)(((rnd(s) >> 32) * (uint64_t)n) >> 32); }

/* pick one of the pseudo-legal moves in m[0..n) at random, make it, retry on
   illegal; returns 0 when none of them is legal */
static inline int make_random(Board *b, Move *m, int n, uint64_t *rng) {
  while (n) {
    int r = rnd_below(rng, n);
    Board save = *b;
    make(b, m[r]);
    if (!illegal(b)) return 1;
    *b = save;
    m[r] = m[--n];
  }
  return 0;
}

static inline int is_capture(const Board *b, Move m) {
  int to = MTO(m);
  return b->sq[to] || (to == b->ep && TYPE(b->sq[MFROM(m)]) == P);
}

static inline int victim(const Board *b, Move m) {
  int to = MTO(m);
  return b->sq[to] ? val[TYPE(b->sq[to])] : (to == b->ep && TYPE(b->sq[MFROM(m)]) == P) ? val[P] : 0;
}

/* direction from a to b along a line, 0 if none; indexed by b - a + 0x77 */
static int8_t ray_dir[240];
static uint8_t knight_hop[240];

static void init_rays(void) {
  int d, k, i;
  for (i = 0; i < 8; i++) {
    for (k = 1, d = k_off[i]; k <= 7; k++) ray_dir[k * d + 0x77] = (int8_t)d;
    knight_hop[n_off[i] + 0x77] = 1;
  }
}

/* does the moved piece attack the enemy king from its destination?
   discovered checks, castling and en passant oddities are ignored */
static inline int gives_check(const Board *b, Move m) {
  int from = MFROM(m), to = MTO(m), c = b->side, ksq = b->list[c ^ 1][0];
  int t = MPROMO(m) ? MPROMO(m) : TYPE(b->sq[from]), d, s;
  switch (t) {
  case P: return ksq == to + (c == WHITE ? 15 : -15) || ksq == to + (c == WHITE ? 17 : -17);
  case N: return knight_hop[ksq - to + 0x77];
  case K: return 0;
  default:
    d = ray_dir[ksq - to + 0x77];
    if (!d) return 0;
    if (t == B && !(d & 15 && d != 1 && d != -1 && d != 16 && d != -16)) return 0;
    if (t == R && (d == 15 || d == 17 || d == -15 || d == -17)) return 0;
    for (s = to + d; s != ksq; s += d) if (b->sq[s] && s != from) return 0;
    return 1;
  }
}

static int step_uniform(Board *b, uint64_t *rng) {
  Move moves[MAXMOVES], *m = moves;
  int n = gen_all(b, m), i, nc = 0;
  if (opt_capture && rnd_below(rng, 100) < opt_capture) {
    if (opt_mvv) {
      int best = 0;
      for (i = 0; i < n; i++) { int v = victim(b, m[i]); if (v > best) best = v; }
      if (best) for (i = 0; i < n; i++) if (victim(b, m[i]) == best) { Move t = m[i]; m[i] = m[nc]; m[nc++] = t; }
    } else {
      for (i = 0; i < n; i++) if (is_capture(b, m[i])) { Move t = m[i]; m[i] = m[nc]; m[nc++] = t; }
    }
    if (nc && make_random(b, m, nc, rng)) return 1;
    m += nc; n -= nc; nc = 0;
  }
  if (opt_check && rnd_below(rng, 100) < opt_check) {
    for (i = 0; i < n; i++) if (gives_check(b, m[i])) { Move t = m[i]; m[i] = m[nc]; m[nc++] = t; }
    if (nc && make_random(b, m, nc, rng)) return 1;
    m += nc; n -= nc;
  }
  return make_random(b, m, n, rng);
}

static int step_piece(Board *b, uint64_t *rng) {
  Move m[32];
  uint8_t cand[16];
  int np = b->n[b->side];
  memcpy(cand, b->list[b->side], np);
  while (np) {
    int r = rnd_below(rng, np);
    if (make_random(b, m, gen_piece(b, cand[r], m), rng)) return 1;
    cand[r] = cand[--np];
  }
  return 0;
}

/* result for the side to move at the start: 2 win, 1 draw, 0 loss */
static int playout(Board b, uint64_t *rng, int *len, int *end) {
  int ply;
  for (ply = 0;; ply++) {
    if (b.hm >= 100) { *end = END_FIFTY; break; }
    if (dead(&b)) { *end = END_DEAD; break; }
    if (ply >= opt_clip) {
      *end = END_CLIP;
      if (opt_clipmat) {
        int d = material(&b);          /* side to move at the clip */
        int r = d > 0 ? 2 : d < 0 ? 0 : 1;
        *len = ply;
        return (ply & 1) ? 2 - r : r;
      }
      break;
    }
    if (!(opt_policy == POL_PIECE ? step_piece(&b, rng) : step_uniform(&b, rng))) {
      *len = ply;
      if (!in_check(&b)) { *end = END_STALEMATE; return 1; }
      *end = END_MATE;
      return (ply & 1) ? 2 : 0;
    }
  }
  *len = ply;
  return 1;
}

/* --------------------------------------------------------------- search */

static Board root;
static Board child[MAXMOVES];
static Move rootmoves[MAXMOVES];
static int nroot;
static Thread *threads;
static int nthreads;
static _Atomic int stop_flag;
static _Atomic int searching;
static pthread_t manager_th;
static long long t_start, t_limit;
static long long lim_nodes, lim_depth;
static pthread_mutex_t out_mutex = PTHREAD_MUTEX_INITIALIZER;

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void sleep_ms(int ms) {
#ifdef _WIN32
  Sleep(ms);
#else
  struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
  nanosleep(&ts, NULL);
#endif
}

static int cpu_count(void) {
#ifdef _WIN32
  SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN); return n < 1 ? 1 : (int)n;
#endif
}

static void out(const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof buf) n = sizeof buf - 1;
  pthread_mutex_lock(&out_mutex);
  fwrite(buf, 1, n, stdout);
  fflush(stdout);
  pthread_mutex_unlock(&out_mutex);
}

static Move replies[MAXMOVES][MAXMOVES];   /* legal replies to each root move, ReplyMin only */
static int nrep[MAXMOVES];

static void *worker(void *arg) {
  Thread *t = arg;
  int i = nroot ? t->id % nroot : 0, len, end, r;
  while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
    if (opt_replymin && nrep[i]) {
      /* opponent's first reply is chosen round-robin, then the game is random from there */
      int j = t->rj[i];
      Board b = child[i];
      make(&b, replies[i][j]);
      r = playout(b, &t->rng, &len, &end);          /* from our side's view already */
      atomic_fetch_add_explicit(&t->rcnt[i * MAXMOVES + j], 1, memory_order_relaxed);
      atomic_fetch_add_explicit(&t->rpts[i * MAXMOVES + j], r, memory_order_relaxed);
      t->rj[i] = (uint8_t)(j + 1 == nrep[i] ? 0 : j + 1);
    } else r = 2 - playout(child[i], &t->rng, &len, &end);
    atomic_fetch_add_explicit(&t->pts[i], r, memory_order_relaxed);
    atomic_fetch_add_explicit(&t->cnt[i], 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&t->games, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&t->plies, len, memory_order_relaxed);
    atomic_fetch_add_explicit(&t->ends[end], 1, memory_order_relaxed);
    if ((uint32_t)len > atomic_load_explicit(&t->maxlen, memory_order_relaxed))
      atomic_store_explicit(&t->maxlen, len, memory_order_relaxed);
    if (++i == nroot) i = 0;
  }
  return NULL;
}

typedef struct {
  uint64_t cnt[MAXMOVES], pts[MAXMOVES], games, plies, ends[END_COUNT];
  double score[MAXMOVES];          /* points per game, or the worst reply's with ReplyMin */
  uint32_t maxlen, mincnt;
} Totals;

static void gather(Totals *tot) {
  int i, j, k;
  memset(tot, 0, sizeof *tot);
  for (j = 0; j < nthreads; j++) {
    Thread *t = &threads[j];
    for (i = 0; i < nroot; i++) {
      tot->cnt[i] += atomic_load_explicit(&t->cnt[i], memory_order_relaxed);
      tot->pts[i] += atomic_load_explicit(&t->pts[i], memory_order_relaxed);
    }
    tot->games += atomic_load_explicit(&t->games, memory_order_relaxed);
    tot->plies += atomic_load_explicit(&t->plies, memory_order_relaxed);
    for (i = 0; i < END_COUNT; i++) tot->ends[i] += atomic_load_explicit(&t->ends[i], memory_order_relaxed);
    uint32_t ml = atomic_load_explicit(&t->maxlen, memory_order_relaxed);
    if (ml > tot->maxlen) tot->maxlen = ml;
  }
  tot->mincnt = UINT32_MAX;
  for (i = 0; i < nroot; i++) if (tot->cnt[i] < tot->mincnt) tot->mincnt = (uint32_t)tot->cnt[i];
  if (!nroot) tot->mincnt = 0;
  for (i = 0; i < nroot; i++) {
    tot->score[i] = tot->cnt[i] ? (double)tot->pts[i] / (double)tot->cnt[i] : -1;
    if (opt_replymin && nrep[i] && tot->cnt[i]) {
      double worst = 3;
      for (k = 0; k < nrep[i]; k++) {
        uint64_t c = 0, p = 0;
        for (j = 0; j < nthreads; j++) {
          c += atomic_load_explicit(&threads[j].rcnt[i * MAXMOVES + k], memory_order_relaxed);
          p += atomic_load_explicit(&threads[j].rpts[i * MAXMOVES + k], memory_order_relaxed);
        }
        if (c && (double)p / (double)c < worst) worst = (double)p / (double)c;
      }
      if (worst <= 2) tot->score[i] = worst;
    }
  }
}

/* best = highest score (points per game, or worst-reply points per game); ties broken at random */
static int best_move(const Totals *tot, uint64_t *rng) {
  int i, best = 0, nties = 0;
  double bestv = -1;
  for (i = 0; i < nroot; i++) {
    double v = tot->score[i];
    if (v > bestv + 1e-12) { bestv = v; best = i; nties = 1; }
    else if (v > bestv - 1e-12 && rnd_below(rng, ++nties) == 0) best = i;
  }
  return best;
}

static void start_workers(void) {
  int j;
  atomic_store(&stop_flag, 0);
  for (j = 0; j < nthreads; j++) {
    Thread *t = &threads[j];
    _Atomic uint32_t *rc = t->rcnt, *rp = t->rpts;
    memset(t, 0, sizeof *t);
    t->rcnt = rc; t->rpts = rp;
    if (opt_replymin) {
      if (!t->rcnt) { t->rcnt = calloc(MAXMOVES * MAXMOVES, sizeof *t->rcnt); t->rpts = calloc(MAXMOVES * MAXMOVES, sizeof *t->rpts); }
      else { memset(t->rcnt, 0, MAXMOVES * MAXMOVES * sizeof *t->rcnt); memset(t->rpts, 0, MAXMOVES * MAXMOVES * sizeof *t->rpts); }
    }
    t->id = j;
    t->rng = (uint64_t)now_ms() * 0x9E3779B97F4A7C15ULL ^ ((uint64_t)(j + 1) << 32) ^ (uint64_t)rand();
    pthread_create(&t->th, NULL, worker, t);
  }
}

static void join_workers(void) {
  int j;
  atomic_store(&stop_flag, 1);
  for (j = 0; j < nthreads; j++) pthread_join(threads[j].th, NULL);
}

static void print_info(const Totals *tot, long long elapsed, int best) {
  long long ms = elapsed > 0 ? elapsed : 1;
  double wr = tot->score[best] >= 0 ? 50.0 * tot->score[best] : 0;
  out("info depth %u seldepth %u score cp %d nodes %llu nps %llu time %lld pv %s\n",
      tot->mincnt, tot->maxlen, material(&root), (unsigned long long)tot->plies,
      (unsigned long long)(tot->plies * 1000 / ms), elapsed, move_str(rootmoves[best]));
  out("info string games %llu (%llu/s) avglen %.0f best %s %.1f%% mates %.1f%% clipped %.1f%%\n",
      (unsigned long long)tot->games, (unsigned long long)(tot->games * 1000 / ms),
      tot->games ? (double)tot->plies / (double)tot->games : 0.0, move_str(rootmoves[best]), wr,
      tot->games ? 100.0 * (double)tot->ends[END_MATE] / (double)tot->games : 0.0,
      tot->games ? 100.0 * (double)tot->ends[END_CLIP] / (double)tot->games : 0.0);
}

static void *manager(void *arg) {
  (void)arg;
  Totals tot;
  uint64_t rng = (uint64_t)now_ms() ^ 0x5851F42D4C957F2DULL;
  long long next_info = 1000;
  start_workers();
  for (;;) {
    sleep_ms(5);
    long long el = now_ms() - t_start;
    if (atomic_load(&stop_flag)) break;
    if (t_limit && el >= t_limit) break;
    if (lim_nodes || lim_depth || el >= next_info) {
      gather(&tot);
      if (lim_nodes && (long long)tot.games >= lim_nodes) break;
      if (lim_depth && (long long)tot.mincnt >= lim_depth) break;
      if (el >= next_info) { print_info(&tot, el, best_move(&tot, &rng)); next_info = el + 1000; }
    }
  }
  join_workers();
  gather(&tot);
  int best = best_move(&tot, &rng);
  print_info(&tot, now_ms() - t_start, best);
  out("bestmove %s\n", move_str(rootmoves[best]));
  atomic_store(&searching, 0);
  return NULL;
}

static void stop_search(void) {
  if (atomic_load(&searching)) {
    atomic_store(&stop_flag, 1);
    pthread_join(manager_th, NULL);
  }
}

static void set_threads(int n) {
  if (n < 1) n = 1;
  if (n > 512) n = 512;
  for (int j = 0; j < nthreads; j++) { free(threads[j].rcnt); free(threads[j].rpts); }
  free(threads);
  nthreads = n;
  threads = calloc(nthreads, sizeof(Thread));
}

static void go(long long wtime, long long btime, long long winc, long long binc, int movestogo, long long movetime, int infinite) {
  int i;
  stop_search();
  t_start = now_ms();
  nroot = gen_legal(&root, rootmoves);
  if (!nroot) { out("info string no legal moves\nbestmove 0000\n"); return; }
  for (i = 0; i < nroot; i++) {
    child[i] = root; make(&child[i], rootmoves[i]);
    nrep[i] = opt_replymin ? gen_legal(&child[i], replies[i]) : 0;
  }
  if (nroot == 1 && !infinite) {
    out("info depth 0 score cp %d nodes 0 time 0 pv %s\nbestmove %s\n", material(&root), move_str(rootmoves[0]), move_str(rootmoves[0]));
    return;
  }
  t_limit = 0;
  if (movetime) t_limit = movetime - opt_overhead;
  else if (wtime || btime) {
    long long t = root.side == WHITE ? wtime : btime, inc = root.side == WHITE ? winc : binc;
    long long alloc = t / (movestogo ? movestogo : opt_divisor) + inc * 3 / 4 - opt_overhead;
    if (alloc > t * 4 / 5 - opt_overhead) alloc = t * 4 / 5 - opt_overhead;
    t_limit = alloc;
  }
  if (t_limit && t_limit < 1) t_limit = 1;
  if (infinite) t_limit = 0;
  atomic_store(&searching, 1);
  pthread_create(&manager_th, NULL, manager, NULL);
}

/* -------------------------------------------------------------- console */

static uint64_t perft(const Board *b, int depth) {
  Move m[MAXMOVES];
  int n = gen_all(b, m), i;
  uint64_t cnt = 0;
  for (i = 0; i < n; i++) {
    Board t = *b;
    make(&t, m[i]);
    if (illegal(&t)) continue;
    cnt += depth > 1 ? perft(&t, depth - 1) : 1;
  }
  return cnt;
}

static void bench(int secs) {
  Totals tot;
  long long t0 = now_ms();
  nroot = 1; child[0] = root; rootmoves[0] = 0; nrep[0] = 0;
  start_workers();
  sleep_ms(secs * 1000);
  join_workers();
  long long ms = now_ms() - t0;
  gather(&tot);
  nroot = 0;
  printf("threads %d policy %s clip %d: %llu games in %lld ms = %llu games/s, %llu plies/s, avg length %.1f, max %u\n",
         nthreads, opt_policy == POL_PIECE ? "Piece" : "Uniform", opt_clip,
         (unsigned long long)tot.games, ms, (unsigned long long)(tot.games * 1000 / ms),
         (unsigned long long)(tot.plies * 1000 / ms), tot.games ? (double)tot.plies / (double)tot.games : 0.0, tot.maxlen);
  printf("ends: mate %.1f%% stalemate %.1f%% fifty %.1f%% dead %.1f%% clipped %.1f%%\n",
         100.0 * (double)tot.ends[END_MATE] / (double)tot.games, 100.0 * (double)tot.ends[END_STALEMATE] / (double)tot.games,
         100.0 * (double)tot.ends[END_FIFTY] / (double)tot.games, 100.0 * (double)tot.ends[END_DEAD] / (double)tot.games,
         100.0 * (double)tot.ends[END_CLIP] / (double)tot.games);
  printf("side to move: %.3f points per game (2 win, 1 draw, 0 loss)\n",
         tot.cnt[0] ? (double)(2 * tot.cnt[0] - tot.pts[0]) / (double)tot.cnt[0] : 0.0);
  fflush(stdout);
}

/* ------------------------------------------------------------------ uci */

static void position(char *s) {
  char *moves = strstr(s, "moves");
  if (moves) *moves = 0;
  if (!strncmp(s, "startpos", 8)) set_fen(&root, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  else if (!strncmp(s, "fen", 3)) set_fen(&root, s + 4);
  if (!moves) return;
  for (char *tok = strtok(moves + 5, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
    Move m[MAXMOVES];
    int n = gen_legal(&root, m), i;
    for (i = 0; i < n; i++) if (!strcmp(move_str(m[i]), tok)) { make(&root, m[i]); break; }
    if (i == n) break;
  }
}

int main(void) {
  static char line[1 << 16];
  int i;
  setvbuf(stdout, NULL, _IONBF, 0);
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  for (i = 0; i < 128; i++) castle_mask[i] = 15;
  castle_mask[0x04] = 12; castle_mask[0x00] = 13; castle_mask[0x07] = 14;
  castle_mask[0x74] = 3; castle_mask[0x70] = 7; castle_mask[0x77] = 11;
  srand((unsigned)time(NULL));
  init_rays();
  opt_threads = cpu_count();
  set_threads(opt_threads);
  set_fen(&root, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

  while (fgets(line, sizeof line, stdin)) {
    char *s = line, *tok;
    s[strcspn(s, "\r\n")] = 0;
    tok = strtok(s, " ");
    if (!tok) continue;
    if (!strcmp(tok, "uci")) {
      out("id name %s\nid author %s\n", NAME, AUTHOR);
      out("option name Threads type spin default %d min 1 max 512\n", opt_threads);
      out("option name ClipPlies type spin default %d min 2 max 10000\n", opt_clip);
      out("option name Policy type combo default Uniform var Uniform var Piece\n");
      out("option name ClipScore type combo default %s var Draw var Material\n", opt_clipmat ? "Material" : "Draw");
      out("option name CapturePercent type spin default %d min 0 max 100\n", opt_capture);
      out("option name ReplyMin type check default %s\n", opt_replymin ? "true" : "false");
      out("option name CheckPercent type spin default %d min 0 max 100\n", opt_check);
      out("option name CaptureBest type check default %s\n", opt_mvv ? "true" : "false");
      out("option name MoveOverhead type spin default %d min 0 max 5000\n", opt_overhead);
      out("option name TimeDivisor type spin default %d min 1 max 200\n", opt_divisor);
      out("uciok\n");
    } else if (!strcmp(tok, "isready")) {
      out("readyok\n");
    } else if (!strcmp(tok, "setoption")) {
      char *name = NULL, *value = NULL;
      while ((tok = strtok(NULL, " "))) {
        if (!strcmp(tok, "name")) name = strtok(NULL, " ");
        else if (!strcmp(tok, "value")) { value = strtok(NULL, ""); break; }
      }
      if (!name || !value) continue;
      stop_search();
      if (!strcmp(name, "Threads")) set_threads(atoi(value));
      else if (!strcmp(name, "ClipPlies")) { opt_clip = atoi(value); if (opt_clip < 2) opt_clip = 2; }
      else if (!strcmp(name, "Policy")) opt_policy = !strcmp(value, "Piece") ? POL_PIECE : POL_UNIFORM;
      else if (!strcmp(name, "ClipScore")) opt_clipmat = !strcmp(value, "Material");
      else if (!strcmp(name, "CapturePercent")) { opt_capture = atoi(value); if (opt_capture < 0) opt_capture = 0; if (opt_capture > 100) opt_capture = 100; }
      else if (!strcmp(name, "ReplyMin")) opt_replymin = !strcmp(value, "true");
      else if (!strcmp(name, "CheckPercent")) { opt_check = atoi(value); if (opt_check < 0) opt_check = 0; if (opt_check > 100) opt_check = 100; }
      else if (!strcmp(name, "CaptureBest")) opt_mvv = !strcmp(value, "true");
      else if (!strcmp(name, "MoveOverhead")) opt_overhead = atoi(value);
      else if (!strcmp(name, "TimeDivisor")) { opt_divisor = atoi(value); if (opt_divisor < 1) opt_divisor = 1; }
    } else if (!strcmp(tok, "ucinewgame")) {
      stop_search();
    } else if (!strcmp(tok, "position")) {
      stop_search();
      tok = strtok(NULL, "");
      if (tok) position(tok);
    } else if (!strcmp(tok, "go")) {
      long long wtime = 0, btime = 0, winc = 0, binc = 0, movetime = 0;
      int movestogo = 0, infinite = 0;
      lim_nodes = lim_depth = 0;
      while ((tok = strtok(NULL, " "))) {
        char *v = NULL;
        if (!strcmp(tok, "infinite")) { infinite = 1; continue; }
        if (!(v = strtok(NULL, " "))) break;
        if (!strcmp(tok, "wtime")) wtime = atoll(v);
        else if (!strcmp(tok, "btime")) btime = atoll(v);
        else if (!strcmp(tok, "winc")) winc = atoll(v);
        else if (!strcmp(tok, "binc")) binc = atoll(v);
        else if (!strcmp(tok, "movestogo")) movestogo = atoi(v);
        else if (!strcmp(tok, "movetime")) movetime = atoll(v);
        else if (!strcmp(tok, "nodes")) lim_nodes = atoll(v);
        else if (!strcmp(tok, "depth")) lim_depth = atoll(v);
      }
      if (!wtime && !btime && !movetime && !lim_nodes && !lim_depth) infinite = 1;
      go(wtime, btime, winc, binc, movestogo, movetime, infinite);
    } else if (!strcmp(tok, "stop")) {
      stop_search();
    } else if (!strcmp(tok, "quit")) {
      stop_search();
      break;
    } else if (!strcmp(tok, "d")) {
      stop_search();
      print_board(&root);
    } else if (!strcmp(tok, "eval")) {
      printf("material %+d (side to move)\n", material(&root));
    } else if (!strcmp(tok, "perft")) {
      stop_search();
      tok = strtok(NULL, " ");
      int d = tok ? atoi(tok) : 1;
      long long t0 = now_ms();
      uint64_t n = perft(&root, d < 1 ? 1 : d);
      long long ms = now_ms() - t0;
      printf("perft %d: %llu nodes in %lld ms (%llu nps)\n", d, (unsigned long long)n, ms, (unsigned long long)(n * 1000 / (ms ? ms : 1)));
    } else if (!strcmp(tok, "bench")) {
      stop_search();
      tok = strtok(NULL, " ");
      bench(tok ? atoi(tok) : 5);
    }
  }
  return 0;
}
