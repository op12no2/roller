# roller

A chess engine with no search. For every legal move it plays random games
to the end and plays the move whose games scored the most points (win 2,
draw 1, loss 0). Monte Carlo tree search without the tree.

The random games are almost uniform: every legal move is equally likely,
except that a side which can capture does so half the time
(`CapturePercent`, see below). That one bias is worth about 300 Elo in
self-play, because in a world where hanging pieces get taken, material
starts to matter.

The only knowledge in it is the rules of chess. A game ends by checkmate,
stalemate, the fifty-move rule or a dead position (bare kings, a lone
minor, same-coloured bishops). Games longer than `ClipPlies` plies are cut
off and count as draws. Repetition is not checked inside the random games;
the fifty-move rule and the clip deal with shuffling.

The score it reports is plain material from the side to move, 1/3/3/5/9.
Depth is the number of games played per root move, seldepth the longest
game, nodes the number of positions played through. An `info string` line
gives games per second, the best move's win percentage and how many games
ended in mate or were clipped.

## Build

    ./build.sh

needs `clang` and the mingw-w64 sysroot (`x86_64-w64-mingw32-gcc` pulls it
in), and produces `roller` (Linux) and `roller.exe` (static Windows, no
dependencies). `CC=gcc ./build.sh` uses gcc instead; clang's binaries run
about 18% faster here.

## Options

| option | default | meaning |
|---|---|---|
| Threads | all logical CPUs | playout threads |
| ClipPlies | 300 | games longer than this are draws |
| Policy | Uniform | `Uniform`: every legal move equally likely. `Piece`: pick one of your pieces at random, then one of its legal moves. Faster, and a different kind of random. |
| ClipScore | Draw | `Draw`: clipped games are draws. `Material`: a clipped game is a win for whoever is ahead in material at the clip. |
| CapturePercent | 50 | with this probability a side that can capture does capture (Uniform policy only). 0 is pure uniform random. Games become short trade-fests that mostly end as dead positions. |
| ReplyMin | false | group each root move's games by the opponent's first reply (chosen round-robin) and score the move by its worst reply instead of its average. A second ply for free, from games already played. |
| MoveOverhead | 50 | milliseconds kept back per move |
| TimeDivisor | 25 | spends remaining time / TimeDivisor + 3/4 of the increment per move |

`go` honours `wtime/btime/winc/binc/movestogo`, `movetime`, `nodes` (total
games), `depth` (games per root move), `infinite` and `stop`.

Console commands: `d` (board), `eval` (material), `perft N`, `bench
[seconds]` (playout speed and how games end from the current position).

## Speed

On a Ryzen 9 9950X, 32 threads, from the start position, clang build:

| setting | games/s | plies/s | avg length | mates | dead | clipped at 300 |
|---|---|---|---|---|---|---|
| CapturePercent 50 (default) | 810k | 148M | 184 | 5% | 78% | 7% |
| CapturePercent 0 | 400k | 110M | 273 | 14% | 11% | 71% |
| Policy Piece, CapturePercent 0 | 700k | 180M | 256 | 19% | 20% | 49% |

Left to run their course (clip 2000) uniform random games average 344
plies: 55% end as dead positions, 24% by the fifty-move rule, 15% in
checkmate, 6% in stalemate. Nearly all the mates happen before ply 300,
which is why the clip costs little.

## What was tried

Measured in 24-game gauntlets against the six retro engines at 10s+0.1
and, where that was too noisy, in self-play with a fixed number of games
per move.

- **Capture bias**: the win. 25% to 100% all beat pure random in the
  gauntlets; 50 to 75 looked best. Head to head at 60k games per move,
  CapturePercent 50 beat 0 by 33-7.
- **Scoring clipped games by material** (`ClipScore=Material`): no
  effect, with or without the capture bias. Most clipped games are
  shuffling with little material left to score.
- **Piece policy**: no effect on results, though it is faster.
- **ReplyMin** (score a move by its worst opponent reply, using the games
  already played): worse, 4.5-15.5 in self-play at 300k games per move.
  The minimum over thirty noisy estimates mostly finds the unlucky one,
  and making the first reply uniform removes the capture punishment at
  the ply where it matters most.
- **clang over gcc**: 18% faster, same code.

## How it plays

Two games each against the six engines in
[retro](https://github.com/op12no2/retro) at 20s+0.2, Roller on 14
threads. Version 1.0 (pure uniform random games) scored 3.5/12, beating
Beal 2-0 and Kotok-McCarthy 1.5-0.5 and losing the rest. Version 1.1 (the
capture bias) scored 6/12: 2-0 against Beal and Kotok-McCarthy, 1-1 with
Bernstein and SOMA, 0-2 against Tech and Turochamp. Every game ended by
mate or a drawn rule.

It sees mates in one and two perfectly well, since every random game from
a mating move is a win. With the capture bias it also notices material,
roughly; without it a missing queen barely changes how a random game ends,
and 1.0 would give one away for a tempo. It likes 1.Nf3 and 1.Nh3 followed
by Rg1. Here 1.1 beats SOMA, with a promotion to a second queen on a8 and
a king hunt that ends on c4:

    1. Nf3 d5 2. Nc3 Qd6 3. d4 Nc6 4. b3 Bf5 5. Bb2 Nf6 6. e3 Ne4 7. Nxe4 dxe4
    8. d5 Nb4 9. Bb5+ c6 10. dxc6 exf3 11. cxb7+ Kd8 12. Qxd6+ exd6
    13. bxa8=Q+ Kc7 14. gxf3 Nxc2+ 15. Ke2 Kb6 16. b4 Kxb5 17. Rab1 Nxb4
    18. e4 Bxe4 19. fxe4 a6 20. Rhc1 Nxa2 21. Rc2 Nc3+ 22. Bxc3+ Kc4 23. Be5# 1-0

Written by Claude (Fable 5.1) for Colin Jenkins's "Retro Fun" tournament.

## Ideas not yet tried

- Prefer the most valuable capture, or captures that are not immediately
  recaptured.
- Prefer checks the way captures are preferred, for more mates in the
  random games.
- Weight the piece choice in the Piece policy by value or mobility.
