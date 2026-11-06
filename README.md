# roller

A chess engine with no search. For every legal move it plays random games
to the end and plays the move whose games scored the most points (win 2,
draw 1, loss 0). Monte Carlo tree search without the tree.

The random games are almost uniform: every legal move is equally likely,
except that a side which can capture does so half the time, taking the
most valuable piece on offer, and a side which can give check does so half
the time (`CapturePercent`, `CaptureBest`, `CheckPercent`, see below).
Those two biases are worth about 500 Elo over pure random in self-play,
because in a world where hanging pieces get taken and exposed kings get
checked, material and king safety start to matter.

The only knowledge in it is the rules of chess. A game ends by checkmate,
stalemate, the fifty-move rule or a dead position (bare kings, a lone
minor, same-coloured bishops). Games longer than `ClipPlies` plies are cut
off and count as draws. Repetition is not checked inside the random games;
the fifty-move rule and the clip deal with shuffling. At the root, a move
that would repeat a position for the third time scores exactly a draw, so
the engine neither stumbles into threefold repetition when ahead nor misses
it when behind.

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
| CaptureBest | true | when the capture roll fires, only captures of the most valuable victim are considered. Beat plain random captures 28-12 in self-play. |
| CheckPercent | 50 | with this probability a side that can give a direct check does so. Discovered checks don't count. 0 turns it off. |
| QueenPromotion | false | random games promote to queens only (the root still considers all four pieces) |
| SafePercent | 0 | with this probability, re-roll up to three times for a move whose destination the opponent does not attack |
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
| defaults (capture 50, best victim, check 50) | 780k | 131M | 168 | 9% | 82% | 2% |
| CheckPercent 0 | 800k | 155M | 193 | 5% | 75% | 9% |
| CapturePercent 0, CheckPercent 0 | 400k | 110M | 273 | 14% | 11% | 71% |
| Policy Piece, no biases | 700k | 180M | 256 | 19% | 20% | 49% |

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
- **Most valuable victim** (`CaptureBest`): when the capture roll fires,
  take the biggest piece on offer. Beat plain random captures 28-12 at 60k
  games per move. Now the default.
- **Check preference** (`CheckPercent`): checks happen with the same
  mechanism as captures, and double the mate rate in the random games.
  Flat against plain random captures (21-19 at 50%), but on top of
  CaptureBest it won twice, 26.5-13.5 and 25.5-14.5. 25% was flat, 75%
  slightly positive. Now the default at 50.
- **More captures**: with CaptureBest on, capturing 75% of the time lost
  15.5-24.5 and 100% lost 4.5-35.5. Too greedy: the games stop being random
  enough to measure anything.
- **Avoiding attacked squares** (`SafePercent`): re-roll a move whose
  destination the opponent attacks. Flat at every rate: 18, 23.5, 21, 19.5
  and 22 out of 40 for 25, 50, 50, 75 and 100 percent, at a 20% speed cost.
  Off.
- **Clip length** at equal time (10s+0.1 self-play, 40 games each): 100
  lost 15.5, 120 scored 21, 150 scored 27.5 and then 14 on the rerun, 200
  scored 22, 600 scored 17.5, all against 300. Flat; shorter games buy more
  of them but say less. 300 stays.
- **Threefold repetition at the root**: a move that repeats a position for
  the third time scores exactly a draw. Rules, not a heuristic, and it
  stops the engine shuffling into a draw when it is ahead; two of the
  twelve gauntlet games had ended that way.
- **Queen promotions only** (`QueenPromotion`): 19-21, nothing. Promotions
  are rare enough in 170-ply games that the one-in-four queen does not
  matter.
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
capture bias) scored 6/12 and 1.2 (best victim, checks) 5/12, which a
12-game gauntlet cannot tell apart; both beat Beal 2-0, took points from
Kotok, SOMA and Bernstein, and lost 0-2 to Tech and Turochamp. The
self-play matches above are the real evidence that each step helped. Every
game ended by mate or a drawn rule.

It sees mates in one and two perfectly well, since every random game from
a mating move is a win. With the biases it also notices material and
exposed kings, roughly; without them a missing queen barely changes how a
random game ends, and 1.0 would give one away for a tempo. Here 1.2 mates
SOMA in 13 moves with a knight pair:

    1. e3 Nf6 2. Nf3 Nc6 3. Ng5 d5 4. Qf3 Qd6 5. Bb5 Qc5 6. Ba4 Qc4 7. b4 b5
    8. Na3 Qh4 9. Nxb5 Ne5 10. Qxd5 Nxd5 11. Nd6+ Kd8 12. Ndxf7+ Nxf7 13. Nxf7# 1-0

Written by Claude (Fable 5.1) for Colin Jenkins's "Retro Fun" tournament.

## Ideas not yet tried

- Prefer captures that are not immediately recaptured, or avoid moving to
  attacked squares (a one-ply lookahead inside the random game).
- Prefer promotions, or escape from check with a capture.
- Weight the piece choice in the Piece policy by value or mobility.
