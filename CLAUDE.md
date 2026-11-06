# CLAUDE.md

Notes for Claude working in this repository. Read before changing anything.

## What this is

Roller is a deliberately quirky UCI chess engine with no search: for every
root move it plays random games to the end and plays the move with the most
points. It was written for the user's "Retro Fun" tournament on Windows
(Myracle GUI, 12 min + 8 s) alongside the recreations in `~/retro`. The
point is the quirk. The constraint is "no heuristics other than the rules
of chess", held loosely: biasing the random move or scoring clipped games
by material are fair game as options, a real search or evaluation is not.
Speed matters: more games per second is the only lever.

Claude wrote it and is the author of record (`AUTHOR` in `roller.c`,
commits, releases). The user owns the repo.

## Where things live on the user's machine

- This repo: `~/roller` (WSL). The sibling `~/retro` holds the recreated
  historical engines and a `CLAUDE.md` with the Myracle details.
- Myracle's engine folders: `/mnt/d/myracle/engines/`. The retro exes live
  in `retro/` there; put `roller.exe` where the user says (not yet placed).
  Myracle refuses to start an engine whose option defaults changed since it
  was added ("defaults have changed"), so do not change option defaults or
  names casually once it is in the tournament.
- fastchess (Linux build) is at `~/fastchess/fastchess` and runs our Windows
  exe under WSL because we force binary stdout.

## Layout

One file, `roller.c`, sections marked by comment banners:

- board: 0x88 mailbox plus per-side piece lists (`list[c][0]` is always the
  king), `gen_piece`/`gen_all` are pseudo-legal, `make` has no undo, legality
  is checked after making with `illegal()`. `dead()` is insufficient
  material.
- playouts: `make_random` picks a random pseudo-legal move, makes it on the
  board, restores a saved copy if the king is left in check, and tries
  again; this is uniform over legal moves. `playout` returns 2/1/0 for the
  side to move at its start. `step_uniform` and `step_piece` are the two
  policies.
- search: `go` builds `child[]` for every root move; worker threads cycle
  over them, one game each, accumulating per-thread atomic stats; a manager
  thread watches the clock and limits, prints `info` once a second and the
  final `bestmove`. `best_move` is highest points per game, ties random.
- console: `perft`, `bench`.
- uci: the main loop. Windows stdout is set to binary in `main`; keep it, or
  Linux-side GUIs read `bestmove e2e4\r` as illegal.

`build.sh` builds `roller` (gcc -O3) and `roller.exe` (mingw-w64, static).
Binaries are git-ignored and shipped as GitHub releases.

## Testing

1. `./build.sh`, then perft if move generation was touched. Known-good:
   startpos depth 5 = 4865609; kiwipete
   `r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1`
   depth 4 = 4085603; `8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1` depth 5 =
   674624; `r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1`
   depth 4 = 422333; `rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8`
   depth 4 = 2103487;
   `r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10`
   depth 4 = 3894594.
2. `printf 'bench 5\nquit\n' | ./roller` for speed and how games end.
   Baseline on the user's Ryzen 9 9950X, 32 threads: Uniform 350k games/s,
   95M plies/s; Piece 610k games/s, 157M plies/s. Single thread about
   4.5M plies/s. A change that costs more than a few percent needs a reason.
3. A fastchess gauntlet with the Windows exe. Run it from a scratch
   directory (fastchess drops a `config.json`), and cap Threads so the
   opponents get CPU:
   `~/fastchess/fastchess -tournament gauntlet -seeds 1 -engine cmd=~/roller/roller.exe name=Roller option.Threads=14 -engine cmd=~/retro/tech.exe name=Tech -engine cmd=~/retro/beal.exe name=Beal -each tc=20+0.2 proto=uci -rounds 1 -games 2 -repeat -concurrency 2 -pgnout file=m.pgn`
   Every game must end by mate, draw or resignation, never by illegal move
   or time loss. Baseline, 2 games each against the six retro engines at
   20+0.2: 3.5/12, beating Beal 2-0 and Kotok 1.5-0.5, losing 0-2 to Tech,
   Turochamp, Bernstein and SOMA. It gives material away freely because
   uniform random games barely register it; that is expected.
4. When piping `quit` after `go`, sleep first; otherwise the search is
   stopped at once.

## Conventions

- Commits are authored by Claude:
  `git -c user.name="Claude Fable 5.1" -c user.email="noreply@anthropic.com" commit`,
  ending with the `Co-Authored-By` trailer the harness asks for.
- Releases carry `roller.exe` and `roller`: `gh release create vX.Y roller.exe roller`
  with a short note. Bump `NAME` in `roller.c` to match.
- Option names are part of the user's tournament config once added to
  Myracle: `Threads`, `ClipPlies`, `Policy`, `MoveOverhead`, `TimeDivisor`.
  Add options rather than renaming.
- Keep it one file, plain C11, no dependencies beyond pthreads.

## Ideas queued (see README "Ideas not yet tried")

Material scoring of clipped games, capture-biased policy, piece weighting,
using the opponent's first reply as a free second ply. Each should be an
option with the current behaviour as default, measured with `bench` and a
gauntlet before and after.
