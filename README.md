# roller

A chess engine with no search. For every legal move it plays random games
to the end, both sides choosing uniformly among their legal moves, and
plays the move whose games scored the most points (win 2, draw 1, loss 0).
Monte Carlo tree search without the tree.

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

needs `gcc` and `x86_64-w64-mingw32-gcc`, and produces `roller` (Linux) and
`roller.exe` (static Windows, no dependencies).

## Options

| option | default | meaning |
|---|---|---|
| Threads | all logical CPUs | playout threads |
| ClipPlies | 300 | games longer than this are draws |
| Policy | Uniform | `Uniform`: every legal move equally likely. `Piece`: pick one of your pieces at random, then one of its legal moves. Faster, and a different kind of random. |
| MoveOverhead | 50 | milliseconds kept back per move |
| TimeDivisor | 25 | spends remaining time / TimeDivisor + 3/4 of the increment per move |

`go` honours `wtime/btime/winc/binc/movestogo`, `movetime`, `nodes` (total
games), `depth` (games per root move), `infinite` and `stop`.

Console commands: `d` (board), `eval` (material), `perft N`, `bench
[seconds]` (playout speed and how games end from the current position).

## Speed

On a Ryzen 9 9950X, 32 threads, from the start position:

| policy | games/s | plies/s | avg length | mates | clipped at 300 |
|---|---|---|---|---|---|
| Uniform | 350k | 95M | 273 | 14% | 71% |
| Piece | 610k | 157M | 256 | 19% | 49% |

Left to run their course (clip 2000) uniform random games average 344
plies: 55% end as dead positions, 24% by the fifty-move rule, 15% in
checkmate, 6% in stalemate. Nearly all the mates happen before ply 300,
which is why the clip costs little.

## How it plays

Two games each against the six engines in
[retro](https://github.com/op12no2/retro) at 20s+0.2, Roller on 14
threads: 3.5/12. It beat Beal (alpha-beta over a random evaluation) 2-0 and
Kotok-McCarthy 1.5-0.5, and lost both games to Tech, Turochamp, Bernstein
and SOMA. Every game ended by mate or repetition.

It sees mates in one and two perfectly well, since every random game from
a mating move is a win. It has almost no sense of material, since a missing
queen barely changes how a uniformly random game ends, so it will give one
away for a tempo. It likes 1.Nf3 and 1.Nh3 followed by Rg1. Here it is
beating Kotok-McCarthy:

    1. Nh3 Nc6 2. Rg1 Nf6 3. Ng5 d5 4. Nxf7 Kxf7 5. Rh1 Bf5 6. h3 Kg8
    7. Nc3 d4 8. b3 dxc3 9. dxc3 Qxd1+ 10. Kxd1 Rd8+ 11. Bd2 e5 12. e4 Nxe4
    13. Bc4+ Rd5 14. Bxd5+ Be6 15. Bxe6# 1-0

Written by Claude (Fable 5.1) for Colin Jenkins's "Retro Fun" tournament.

## Ideas not yet tried

- Score clipped games by material instead of as draws.
- Bias the random move: prefer captures, or prefer the piece with fewer
  moves, or weight pieces by value.
- Reuse the games: the first reply's results are a free second ply.
