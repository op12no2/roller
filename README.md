# Roller 

<p>Roller is a fun and very weak little Javascript UCI chess engine that only knows the rules (except 3-fold repetition).  It has absolutely no semantic knowledge at all, not even the relative piece values.  It plays by performing thousands of totally random games (rollouts) from the current position choosing root moves at random.  There is no search strategy at all.  In effect it's a <a href="https://en.wikipedia.org/wiki/Monte_Carlo_tree_search#Pure_Monte_Carlo_game_search">"pure" MCTS algorithm</a>.  Wins, losses and draws are scored 1, -1, 0 and it simply chooses the root move with the highest average score.  Obviously at the start of a game this is esentially just random play, but it can avoid mate and give mate when they are clearly in sight.</p><p>You can play Roller here:-</p>
<p><a href="https://op12no2.github.io/roller/">https://op12no2.github.io/roller/</a></p>
<p>The "kr/s" figure is showing thousands of rollouts (complete random games) per second.</p>
<p>Interestingly when pitted against a true random mover version of itself, Roller wins all of the games, other than sometimes accidentally falling into 3-fold repetition because it does not know that rule.</p>
<p>Roller can be used in Arena etc like <a href="https://github.com/op12no2/lozza">Lozza</a>.</p>
<p>Roller does not terminate at known draws like KK for example, because that is definitely feeding in knowledge; however tempting it is to do so in the name of kr/s.</p>
<p>You can change the number of seconds Roller takes to move from the entry field above the board, which by default is 10s.</p>
<p>While thinking Roller displays a status above the board of the form:-</p>
<p>time so far (seconds) | <br />thousands of rollouts (games) so far |<br />thousands of rollouts per second so far |<br />best move so far</p>
<p>After moving Roller displays information for each root move:-
<p>move, average length of rollout (plys), total score and number of games, average score.</p>
<p>**** depicts the best score.</p>

## Play roller online

https://op12no2.github.io/roller

## Acknowledgements

https://github.com/oakmac/chessboardjs

https://github.com/jhlywa/chess.js

https://github.com/jquery/jquery

https://github.com/topics/bootstrap
