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
- playouts: `gives_check` is a direct-check test (moved piece attacks the
  king from its destination, via the 0x88 delta tables `ray_dir` and
  `knight_hop` built in `init_rays`); it ignores discovered checks on
  purpose, verified exact otherwise against make+in_check. `make_random` picks a random pseudo-legal move, makes it on the
  board, restores a saved copy if the king is left in check, and tries
  again; this is uniform over legal moves. `playout` returns 2/1/0 for the
  side to move at its start. `step_uniform` and `step_piece` are the two
  policies.
- search: `position` records a hash of every game position since the last
  irreversible move in `hist[]`; `go` flags root moves whose child position
  appears there twice already (`rep_draw`), and `gather` scores those as
  exactly a draw. `go` builds `child[]` for every root move; worker threads cycle
  over them, one game each, accumulating per-thread atomic stats; a manager
  thread watches the clock and limits, prints `info` once a second and the
  final `bestmove`. `best_move` is highest points per game, ties random.
- console: `perft`, `bench`.
- uci: the main loop. `go ponder` is accepted and the token ignored (it
  used to swallow the next token and break the clock parse). Windows stdout is set to binary in `main`; keep it, or
  Linux-side GUIs read `bestmove e2e4\r` as illegal.

`build.sh` builds `roller` and `roller.exe` with clang (`--target=x86_64-w64-mingw32`
for the exe, static); clang measured 18% faster than gcc on both targets.
`CC=gcc ./build.sh` falls back to gcc and mingw-gcc.
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
   Baseline on the user's Ryzen 9 9950X, 32 threads, clang build, 1.4
   defaults (CapturePercent 50, CaptureBest, CheckPercent 50, MateFirst,
   CheckSafe): about 560k games/s, 93M plies/s, 63% of games end as dead
   positions, 23% in mate. No biases:
   400k games/s, 110M plies/s. Single
   thread about 5M plies/s. A change that costs more than a few percent
   needs a reason, but speed is not strength: test with games. Bench
   numbers drift 10% with machine state; compare against the previous
   release binary in the same sitting, not against the numbers here.
   Hyperthreading pays: 32 threads give 1.5x the games of 16.
3. A fastchess gauntlet with the Windows exe. Run it from a scratch
   directory (fastchess drops a `config.json`), and cap Threads so the
   opponents get CPU:
   `~/fastchess/fastchess -tournament gauntlet -seeds 1 -engine cmd=~/roller/roller.exe name=Roller option.Threads=14 -engine cmd=~/retro/tech.exe name=Tech -engine cmd=~/retro/beal.exe name=Beal -each tc=20+0.2 proto=uci -rounds 1 -games 2 -repeat -concurrency 2 -pgnout file=m.pgn`
   Every game must end by mate, draw or resignation, never by illegal move
   or time loss. Gauntlets of 24 games are noisy (the same config scored
   11/24 and 6.5/24 in two runs), so for comparing options prefer
   self-play with a fixed `nodes=` budget (nodes = games per move for this
   engine), which is also immune to CPU contention:
   `~/fastchess/fastchess -engine cmd=./roller.exe name=New option.X=... -engine cmd=./roller.exe name=Old -each nodes=60000 proto=uci option.Threads=7 -rounds 20 -games 2 -repeat -concurrency 4 -pgnout file=sp.pgn`
   Results so far (40-game self-play at 60k games/move unless noted):
   CapturePercent 50 beat 0 by 33-7; CaptureBest beat random captures
   28-12; CheckPercent 50 on top of CaptureBest won 26.5 and 25.5 (25 was
   flat at 18 and 16, 75 scored 22); CapturePercent 75 and 100 with
   CaptureBest lost 15.5 and 4.5; MateFirst won 30 and 32, CheckSafe 26
   and 29, both together 36.5; CaptureSafe 20; QueenPromotion 19; SafePercent flat at
   every rate (18 to 23.5 over five runs); ClipPlies 100 to 600 flat at
   equal time (150 scored 27.5 then 14: that is what 40-game noise looks
   like); ReplyMin lost 4.5-15.5 at 300k
   games/move; ClipScore=Material and Policy=Piece made no difference in
   gauntlets. Each default change was then checked with the 12-game retro
   gauntlet at 20+0.2 on 14 threads: 1.0 scored 3.5, 1.1 scored 6, 1.2
   scored 5, 1.4 scored 6 (noise; trust the self-play). Version 1.0 (pure
   uniform) scored 3.5/12 against the retro six at 20+0.2 on 14 threads.
   The README's "What was tried" section records findings; keep it current.
4. When piping `quit` after `go`, sleep first; otherwise the search is
   stopped at once.
5. Clock: at 120+2 on 16 threads Roller used 172 s of the 202 s available
   over a 41-move game, 6.7 s on the first move and never more, 3 s at
   the end. Allocation is remaining/TimeDivisor + 3/4 increment, capped at
   80% of remaining, minus MoveOverhead. At the tournament's 12 min + 8 s
   the first move takes about 35 s. Re-check this after any change to `go`.

## Conventions

- Commits are authored by Claude:
  `git -c user.name="Claude Fable 5.1" -c user.email="noreply@anthropic.com" commit`,
  ending with the `Co-Authored-By` trailer the harness asks for.
- Releases carry `roller.exe` and `roller`: `gh release create vX.Y roller.exe roller`
  with a short note. Bump `NAME` in `roller.c` to match.
- Option names are part of the user's tournament config once added to
  Myracle: `Threads`, `ClipPlies`, `Policy`, `ClipScore`, `CapturePercent`,
  `CaptureBest`, `CheckPercent`, `CaptureSafe`, `CheckSafe`, `MateFirst`,
  `QueenPromotion`, `SafePercent`, `ReplyMin`, `MoveOverhead`, `TimeDivisor`.
  Add options rather than renaming.
- Keep it one file, plain C11, no dependencies beyond pthreads.

## Ideas queued (see README "Ideas not yet tried")

Avoiding recapture or attacked squares inside the random game, promotion
preference, weighted piece choice.
Each should be an option with the current behaviour as default, measured
with `bench` and self-play before and after, then documented in the README
whether it worked or not.
