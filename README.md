# Roller 

<p>Roller is a fun and vert weak little Javascript chess engine that only knows the rules (except 3-fold repetition).  It has absolutely no semantic knowledge at all, not even the relative piece values.  It plays by performing thousands of totally random games (rollouts) from the current position.  There is no search strategy as such at all.  In effect it's a <a href="https://en.wikipedia.org/wiki/Monte_Carlo_tree_search#Pure_Monte_Carlo_game_search">"pure" MCTS algorithm</a>.  Wins, losses and draws are scored 1, -1, 0 and it simply chooses the move with the highest net score.  Obviously at the start of a game this is mostly just random play but it can avoid mate and give mate when they are clearly in sight.</p><p>You can play Roller here:-</p>
<p><a href="https://op12no2.github.io/roller/">https://op12no2.github.io/roller/</a></p>
<p>The "kr/s" figure is showing thousands of complete rollouts (random games) per second.</p>
<p>Interestingly when pitted against a true random mover version of itself, Roller wins all of the games, other than sometimes accidentally falling into 3-fold repetition because it does not know that rule.</p>
<p>Roller can be used in Arena etc like <a href="https://github.com/op12no2/lozza">Lozza</a>.</p>
<p>If rollouts last longer than 450 ply, a draw is assumed, as some crazy games can last longer than 1000 ply.  This does kinda of compromise the no-knowledge (or "zero") element, but I can live with it.</p>
<p>It does not terminate at known draws like KK for example, because that is  definitely feeding in knowledge; however tempting it is to do so in the name of kr/s.</p>
<p>You can change the number of seconds Roller takes to move from the entry field above the board, which by default is 10s.</p>
<p>While thinking Roller displays a status above the board of the form:-</p>
<p>time so far (seconds) | <br />thousands of rollouts (games) so far |<br />thousands of rollouts per second so far |<br />best move so far</p>
<p>After moving Roller displays the net score and number of rollouts for each  legal move, for example:-</p>
<pre>...<br />e6 9 / 1150<br />d5 31 / 1192 ****<br />d6 -8 / 1197<br />...</pre>
<p>**** depicts the best score and the move chosen.</p>
<p> </p>

