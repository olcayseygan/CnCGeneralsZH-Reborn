# Command and Conquer: Generals: Zero Hour: Reborn

EA released the 2003 source for preservation. It did not compile, did not run, and nobody had touched
the bugs inside it in twenty-two years. This build compiles, runs and plays.

**119 changes. ~580 engine source files ported. 14 automated test suites. Around 60 original bugs
found and fixed â€” EA's own, not port damage.**

---

## The frame rate cap is gone

- The picture now runs uncapped; the rules keep their own steady clock.
- A slow moment costs you a dropped frame, not a slow game.
- Every animation runs on a clock instead of counting frames.
- Briefing and cutscene subtitles hold long enough to read again.
- The radar's under-attack pulse throbs instead of strobing, and no longer ends early.
- The main menu surf rolls at its proper pace, not ten times too fast.

## The computer opponent builds a base now

- One wrong value meant the AI built only urgent items and one power plant.
- Rotated AI bases were laid out wrong; buildings now face the right way.
- An AI with no buildings no longer aims everything at the map corner.
- The AI checks ten seconds of supply-line damage, not a third of one.
- One AI decision read leftover memory, so identical matches played out differently.
- Difficulty and AI money change build pace again; every order was pinned at three seconds.
- The computer no longer shoots at what it cannot see. Its units, and its base defences, used to auto-target through the fog of war - a hard exemption written into the code for computer players only. Stealth works against it now, and so does anything it has not scouted.
- It scouts. There was no such thing as scouting in the computer opponent - it never needed to look, because it could already see everything. One cheap unit now tours the enemy start positions and keeps going round for the rest of the match, replaced out of spare change when it dies. Every difficulty scouts; an opponent that never looks reads as broken, not as easy. And it tours on purpose rather than in a circle: it goes to whichever base it has gone longest without seeing, nearest first, and it does not walk across the map to look again at something it looked at a minute ago - it stays and watches instead. Two scouts never trail each other round the same lap.
- It takes the oil derricks instead of blowing them up. An oil derrick pays whoever owns it and costs one infantryman to walk in - and the computer used to march past the neutral ones and shell yours, which is the one thing you can do with a derrick that earns nobody anything. It now keeps a cheap infantryman doing the rounds of every derrick, refinery and hospital it has found, takes them, and when there is nothing left to take it leaves him standing on the last one instead of walking him home. And it no longer picks a capturable building as a target of its own accord: it can still be ordered to level one, and one that shoots back is still shot back at.
- Nor does it know which start position you took. It used to read that straight off the lobby, for every player, from the first second. Now it works it out: each position it has not looked at is a suspect, and the odds are simply how many opponents are still unaccounted for over how many places they could be - three of you on an eight-position map is 3 in 7 for each. Every empty position its scout crosses off makes the rest likelier, 3 in 6, then 3 in 5. When the numbers meet, it stops looking: three opponents with three places left to be is not a guess, and walking over to confirm it would waste the trip. On a two-player map that is true from the start, so nothing changed there - it never wastes a second hunting for an address it can work out by subtraction. Where it has not worked you out yet, it attacks the position you are most likely to be on, and finds out on arrival.
- Nor does it read your base off the map. Where your base is, what it is worth, which supply dock to expand to and where to aim a superweapon all came off a walk of your object list, in the shroud, from the first second of the match. The computer now only counts what it can see, plus the buildings it has already found - buildings do not walk away. Before it has scouted you, all it knows is where you started, which is on the map preview anyway.

## Generals powers the computer actually meant to buy

- It spent every promotion point the moment it had one, on whatever in its list happened to be cheap enough. The strong three-point ability at the end of the list was never reached, because the points were always already gone.
- From Medium up it saves. If the next thing in its own list is only out of reach on points, it waits for them instead of buying filler.
- Which list it draws from now follows its personality, so the powers coming at you tell you which kind of opponent you are facing.

## The computer comes in where you are thin

- The game has kept a value-and-danger map of every square of the battlefield, per player, since 2003. It has a query interface and even a debug view. Nothing in the computer opponent had ever read it - its only users were two map-script actions almost nobody used.
- At the top level the opponent now aims its attacks at where your money is rather than at the middle of your buildings, which is also the middle of your defences.
- It reads that map through the fog like everything else: only ground it has actually seen counts. An opponent that has not scouted you still attacks the old way.

## The computer expands, and defends what it takes

- It never decided to expand. The machinery was all there - place a supply centre beside a pile, pick a pile worth taking, send a team to sit on it - and every piece of it only ran when a map script said so. An opponent that ran its starting piles dry simply stopped earning.
- It now goes and takes the money that is lying around, out of spare cash so the army never pays for it.
- On Brutal the expansion comes with a defence structure facing the enemy, placed in the same job. An undefended expansion is a gift.
- Measured: a third more army in the field and a quarter more money spent over the match.

## The computer spends its money

- It no longer sits on a pile of cash. Past a level that depends on the difficulty, the more money it has the faster it builds - twice the pile, half the wait, and it stops at four times. It is spending sooner, not building faster: the rate itself is untouched.
- It puts more harvesters on a supply centre with more piles around it, instead of the same three numbers everywhere.

## The computer picks its fights
- Which enemy it goes after was the nearest one and nothing else, plus a rule with its sign the wrong way round: an opponent who had lost his units or his production had his distance treated as half the map, so the computer ignored the one it was about to beat. That is what dragged matches out.
- It now weighs distance, whether the target is crippled - an opportunity, not a distraction - and how much of what it can see that player is worth. Only a genuinely finished enemy is skipped.
- It still refuses to gang up with another computer opponent on one victim, and still gently prefers whoever is already shooting at it.
- An aggressive computer team is supposed to fight its way to where it is sent rather than walk there. It never did. The order to fight was being reissued every frame, and each reissue pushed the moment that unit next looks for a target one step further into the future, so the look never came at all and the team crossed the map through the enemy without firing. Over four and a half minutes of one test match that order was restarted 2,240 times. It is restarted 15 times now.
- And when that switch does happen, the old order stops there. It used to finish its own turn afterwards, steering one last time toward the place the unit was going before the order changed, which is a visible flinch at the head of a column.
## The computer knows when to quit

- Its teams fought to the last man. The word "retreat" appeared nowhere in the opponent's code - the single most visible thing that made it look stupid.
- It now measures the fight, not the health bar: how long its force lasts against how long it needs to finish yours. A unit at a fifth of its health that still out-damages what is shooting it stays; a full-health one being melted leaves. A health percentage gets both of those backwards.
- Two levels of it. On Medium a unit that is personally finished pulls out of a fight its team is still winning. On Brutal the whole team breaks off when the exchange is lost. Easy never quits - that is part of what makes it easy.
- It only counts what it can see. An opponent that flinches from something it has not found is reading your unit list again.
- It weighs a fight by what is in it, not by who is nearby: only things that can shoot count, and buildings do not. Its own base is not a reason to feel safe and yours is not a reason to run.
- Its aircraft are left out of it. A retreat order was what took a parked jet off the runway, so while a fight raged near the airfield the whole wing took off, flew at the base, landed, and did it again every few seconds instead of ever reaching you. Aircraft already fly home on their own when the load is spent.
- Matches finish. Two of these opponents used to fail to settle 65% of their games inside sixteen minutes; it is half that now, and they end in under seven minutes on average instead of ten.

## The computer builds against what you field

- Which unit it trained next was a coin flip. It gathered the teams sharing the highest priority number in its data and picked one at random - not one line looked at what it was fighting. An opponent facing nothing but aircraft went on building tanks.
- Now the priority is the start of a score, not the whole of it. What it can *see* you fielding weights the choice: air pushes anti-air up, stealth pushes detectors up, armour pushes anti-tank up. Its own data decides what counts as the answer to what, so a mod's units are read correctly too.
- How much that weighs depends on the level, from nothing at all on Easy - which is what the game always did - to fully on Brutal.
- Anti-air was the computer's oldest hole. It is closed.

## Three difficulty levels, and none of them cheat

- Easy, Medium, Brutal, the three the game has always had. There were six for a while, with a rung either side of Medium and one above Brutal; they are gone again, and nothing above Brutal went with them - Brutal now plays what the top of that six-rung ladder played.
- Every level plays by your rules. No level gets extra money, cheaper units, faster building, longer vision or tougher units - in either direction. What changes is what the computer is allowed to decide: how often it looks at the map, how long it takes to react, whether it counters what you field, whether it masses before attacking, whether it pulls damaged units out.
- Easy looks around and answers far too late. Medium reacts in time, saves its damaged units and goes out to take a second supply pile. Brutal is the opponent with nothing held back: no reaction delay at all, it builds against what it can see you fielding, it gathers an army before it commits it, and it takes a losing team off the field.
- Measured, not asserted: Brutal beats Easy 15-0 over 32 headless matches with the seats swapped both ways, on twice the army and twice the spending.
- The ladder reads as one ladder wherever a seat is listed. Half of it was named in EA's old words - "Easy Army", "Medium Army" - and half in ours, in the same drop-down, because four separate lists answered "what is this seat called" and no two of them agreed. Every one of them says Easy AI, Medium AI, Brutal AI now: the lobby, the seat itself, the game info panel and the online browser.
- All three are in the network and online lobbies as well as skirmish, and the level the host picks is the level every machine at the table plays. A seat also keeps its name, its colour and its start position when the list refreshes, which it did not.
- Making Easy easier means giving it worse decisions, never less money. That is the whole promise.
- The computer also picks a personality each match and keeps it: one plays for the attack, the other for the base. Same resources, spent differently.

## Attack-move actually attacks

- Ctrl on the click advances the group at the slowest unit's pace.
- A plain click lets each unit run at its own speed.
- A unit on attack move looks half as far again as it can shoot, and nothing to do with how far it can see. The two have nothing to do with each other: artillery outranges its own eyes and found nothing this way, while a scout with a long view stopped for things it would spend a minute driving to. One rule, measured against the gun.
- An aircraft only makes that long look forward. It cannot stop and it turns in a wide circle, so a target off to one side or behind is one it has to come all the way round for - which is how a flight of bombers ends up orbiting a corner of the map instead of arriving. Inside sixty degrees of the nose it looks the full distance; outside it, no further than it can already shoot, so it still takes what it is passing.
- Attack-moving through a base shoots the base; buildings were filtered out.
- Aircraft make their pass, fly home, rearm and resume your order.
- Nothing walks off the map chasing a target that keeps retreating.
- Being pulled off a chase no longer blinds a unit on the way back. It owes the order a stretch of ground before it is allowed to drive to another fight, and it used to spend that stretch refusing to see anything at all - so a tank rolled past an enemy parked beside it without firing a shot. It still will not go chasing during that stretch. It will shoot whatever is already in front of its gun, which costs the advance nothing.
- A unit turns onto whatever is actually shooting it, then resumes the advance.
- Otherwise it picks the worst thing in range: anything armed before anything that is not, then the more dangerous of the two, and worth fades with distance so it is never the far one it walks the field for. A group ordered through a base no longer stops for the first dozer it passes while the artillery beside it keeps firing.
- Ctrl on the attack-move click no longer shells the ground instead.
- A big selection arrives as a crowd. Two hundred units told to attack-move used to spread along a line and three hundred told to move mostly stood still: the group tried to hold its shape all the way in. Past forty units everyone now heads for the spot you clicked and takes the nearest free ground to it. Below forty the group keeps its spacing on the way in, because that is what stops a dozen tanks arriving in single file. A waypoint queued with alt is the exception and now keeps every unit's own spot: only the last point of a queued route gets moved to free ground, so sending a dozen tanks through the same intermediate cell left eleven of them unable to reach it, and the group walked to the first waypoint and stopped there.
- Troop Crawlers wait for their squad to climb back in, up to ten seconds.
- One man who cannot get back aboard no longer parks the Crawler for the rest of the attack move.
- Their squad stays out until the area is clear, instead of piling back in after every kill.
- "Clear" now means as far as the men can shoot, so they finish the enemies in front of them.
- Every man aboard gets out to fight; nobody sits out the battle waiting to be patched up.
- A Crawler that has lost its whole squad carries on with your order instead of idling.
- Attack-move state is saved now, instead of coming back as random memory.

## Your units stop shooting corpses

- Damage counts the moment a shot leaves the barrel, not when it lands.
- Once an enemy is accounted for, the rest of the group retargets.
- Your own right-click orders always win, however much fire is already inbound.
- A reservation lapses within half a second if the shot never arrives.
- Ctrl+Q now takes infantry only, not every armed thing you own.
- Ctrl+D takes every unit of the kinds you have selected that is on the screen, and pressed twice inside half a second takes them across the whole map. One tank selected and two keystrokes is your whole armour, wherever it is standing. It was on Shift+Ctrl+E, which is three fingers for something you do in every fight, and it only ever reached across the map when the screen had nothing left to give - so the wide selection happened to you instead of being asked for.

## Aircraft, guards, and orders that used to be ignored

- Two jets on one airfield could deadlock waiting for each other; now they go.
- Losing the airfield no longer leaves its aircraft circling rubble forever.
- Repaired aircraft fly to the rally point instead of hovering over the pad.
- Infantry leaving a captured building follow its rally point too.
- A Chinook unloads its passengers one at a time, not stacked in one frame.
- A guarding unit no longer fights itself between returning to post and shooting back.
- Engineers can clear mines and booby traps they cannot see.
- A helicopter no longer boards a transport from the air.

## The same game on both screens, and after loading

- A unit with nowhere to stand appears where asked, not at a random point.
- Loading a save remembers what your guards were guarding.
- A turret loaded from a save is still tracking what it was tracking, and a worker loaded mid-job carries on with it, instead of both snapping back to their default state.
- A shell already in the air finishes its flight after a load instead of going off in the launcher's face.
- An aircraft carrier remembers the order it was given, and each runway its own ramp.
- A barracks that has already put its mob on the street does not put a second one out after a load.
- Effects tied together - a smoke column and its embers - stay tied together across a save.
- A loaded save puts the world back in the order it was saved in, so the match plays on from where it was instead of resolving everything backwards.
- And it shows you your own memory of the fog, not everyone else's: a loaded game used to draw what every player remembered on top of the world.
- Saved units come back with the map's weapons, not the stock ones.
- Restarting a skirmish restarts the same skirmish, with a replay that plays back.
- Replays sound and look the way they did when you played them.
- Restart keeps the same random colours, starting corners and armies.
- Somebody quitting no longer reports a desync to everyone still playing.
- A power sabotage no longer follows a player into the next match.
- Muting sound effects or speech no longer desyncs the game.
- Pausing no longer leaks a little memory for every sound that was about to start.
- A sound cancelled before it started really is cancelled. The check compared a pointer against a handle - two different things sharing one slot - so it never matched and the sound played anyway.
- Angles come from the game's own table, identical on every machine.
- Defeat, second maps and the sync fingerprint each had a drift bug; fixed.
- The mismatch check now covers whole units and all sixty-eight special powers.
- Order numbering no longer scrambles your commands after about fifteen hundred orders.
- A network game runs at the speed it says it does.
- A frame of orders now travels in one datagram instead of four or five: the packet was still the 476 bytes a 2003 modem could carry, and every extra datagram was another chance to arrive late.
- Input delay is measured from simulation speed now, not your graphics card.
- One lost packet costs a round trip instead of a flat two seconds.
- A desynced match stops at once instead of playing on as two games.
- A single lost packet no longer freezes the match for twenty seconds. The game now asks for the
  missing frame back after a fifth of a second instead of waiting out the disconnect timer, so
  nobody sits in front of a vote screen for a player who never went anywhere.
- A freeze no longer costs everyone two seconds of input delay for the rest of the match: time
  spent stalled is not mistaken for how slow the connection is.
- A player leaving is reported as a player leaving, not a desync.
- A desync now writes a per-object report on both machines for comparison.
- LAN refuses to start between two machines whose game files differ.
- A player whose name holds a comma, a colon or a control character is turned away at the door instead of scrambling the lobby's idea of who is in the room.
- An open seat can be taken. The map's number of starting positions used to overrule the host: a four position map turned away the fifth player even with a seat left open for him, and that seat stayed open on everyone's screen.
- A map sent to you over the network has to be a map. The other machine used to name any file it liked and fill it with anything at all, and it was written where the name pointed; now the name cannot leave the map folder, the kind has to be one a map transfer carries, and the contents have to match the kind.
- The processor's rounding mode is reset from the right register every frame.
- The disconnect screen no longer interrupts a game that is merely slow.
- The keepalive interval setting is read now, and kept in a sane range.
- The anti-freeze brake is measured properly and comes on twice as early.
- Online input delay is now less than half what it was.
- The room climbs back to full speed after one player's brief hiccup.
- Orders are refused unless the sender controls what they name.
- A start-game command mid-match can no longer strand a player in limbo.
- Corrupted-order repair works in the first two seconds of a match too.
- Losing the relay player no longer picks a player who never existed.
- Order confirmations are filed in one step instead of scanning the whole queue.
- A dropped order's retry wait stops doubling after two steps.

## Sharper textures, for free

- 481 base-game textures at four times the resolution now beat Zero Hour's downscaled copies.
- A long thin texture loads at the size it was drawn at. Anything wider than eight to one used to be stretched onto a bigger, blurrier one, because that was the limit of a 2002 graphics card; the card is asked now, and modern ones have no such limit.

## Every replay, not just the last one

- Replay archiving saves each match under its own date-and-time name.
- Long lists scroll the whole way, past two thousand rows.
- Map, skirmish and replay menus read the catalogue once instead of rebuilding it.

## Placing a building, then changing your mind

- Cancelling a placement no longer deselects the builder.
- The building on your cursor shows where its units will come out.
- An ordered building stands as a see-through plan until a worker starts it.
- Click a plan, or press Stop, to cancel it and take the money back.
- Cancelling a plan is silent â€” nothing was built, so nothing explodes.
- Ground your units have walked stays buildable after they leave it.
- A plan opens no fog of its own, and your opponent never sees it.
- And nothing shoots at one. A plan is invisible to the other side, so a tank that stopped and
  opened fire on empty ground was telling that player where your base was going up before a single
  wall existed. The first work a builder puts in ends this and the site is a target like any other.
- Your units walk straight over a plan â€” nothing solid is there until the builder starts work.
- The plan turns solid the instant the first work goes in, so no building goes up inside its own ghost.
- The builder starts work from where it reaches the site instead of shuffling into place first.
- Dozers and workers walk through anything still going up, so they never shut themselves in behind
  their own work.
- The stop button prints its key on it, like the build buttons do.
- A captured worker stops working for its old owner. Being captured cleared its orders but not its
  job, so it carried on building and repairing for the player who just lost it.

## Bonuses that come and go when they should

- Horde, nationalism and fanaticism bonuses now leave with the horde.
- Fanaticism works without nationalism being bought first.
- Two battle plans stack properly, and plans move with a captured strategy center.
- The Bombardment Cannon cannot be fired in the middle of a battle plan change. The turret was only
  switched off once it had finished swinging back, so the whole swing was still a firing window.
- Passengers do not step out of a transport that is itself inside something. They would have
  appeared inside whatever was carrying it.

## Weapons and units that were quietly wrong

- A bunker buster shot down on the way in no longer clears out the building it was aimed at. It emptied the bunker from wherever it happened to be destroyed - a Point Defence Laser did not save you, it just moved the explosion.
- A Battle Bus in its wrecked form takes attack orders again. Every order you gave it was dropped on the way, because the crew inside are 'held' and held units were skipped.
- A sniper cannot pick at an empty Stinger Site. The rule that stops snipers hurting empty buildings only counted passengers, and a Stinger Site's men are spawned, not carried.
- A booby trap shot off a building, or sold with it, releases the building. The mark was only ever cleared when the trap went off, so the building stayed marked for the rest of the match and no second trap could be laid.
- Something flagged unselectable is unselectable. The flag existed and nothing read it.
- Double-clicking to grab everything of a kind grabs what a box would, not things you can only click one at a time.
- A bounty pays what the percentage says. Rounding up a fraction that was already a whole number paid a dollar extra on every kill.
- A supply upgrade pays for what arrives. The bonus was a flat sum handed over on arrival, so a truck turning up with one box collected the same as a full load - and a driver dropping off little and often earned several times what the upgrade is worth.
- Taking over a defeated ally's base no longer starts his research again from scratch. A player upgrade is bought once for the whole player, and the buildings you inherit were left paying for a second copy of what you already had.
- A tank drives out the moment it is paid for. The factory doors only began opening once the vehicle was already finished, and it stood inside waiting for them, so every single unit cost its build time plus the whole door animation on top - on every tank, all game. The doors now start moving during the last seconds of the build and are open on the frame the unit is done. And they stay open while there is another vehicle behind him, however long that takes: a factory working through a queue keeps its doors up and shuts them when the queue runs out, instead of dropping and hauling them back for every unit. That was worth watching in its own right - there is no artwork for a door that changes its mind partway, so a door caught mid-close and pulled back open snapped wide in a single frame.

## One crate, one collector

- A crate pays once per frame, not once per soldier who touched it.
- A thrown vehicle's crash damage hits the pile once, not once per unit.
- A dying unit is no longer promoted by its last kill.
- Healing no longer counts as damage, so a medic stops revealing its stealth unit.

## Circles are round, and a tunnel is one tunnel

- Circular range checks are circles now, not the square drawn around them.
- A tunnel network heals at one rate, however many entrances it has.

## The power bar tells the truth

- An EMP on an upgraded plant no longer takes the upgrade off your grid twice.
- An EMP on a building site no longer moves power that does not exist yet.
- Loading a saved game keeps disabled power plants disabled.
- Control Rods finishing during a blackout is no longer credited twice.

## Your whole base, on one strip

- One row above the command bar carries everything your base is building anywhere.
- Ordered by time left, so it reads as the order things arrive.
- Select a building and what it is making moves to the front of that row, ahead of everything else, still soonest first among itself. It used to get a whole second row of its own under the base's, which said the same thing twice and pushed the strip further up the screen; now the head of the strip is the building you are looking at and the tail is the rest of your base.
- Click a picture to jump the camera there; Ctrl-click cancels it.
- One picture is one order, cut to match the command bar's artwork.
- The strip is half the size it was: it says the same thing and takes back the screen it was eating.
- Whatever is actually being built counts down inside its own picture. The ones queued behind it stay blank, because their wait depends on everything in front of them.
- A unit finishes walking out of its factory before it takes an order.
- An upgrade stays available if any selected building can still buy it.
- One right click cancels one thing, counted the moment you press.
- Buying an upgrade for a group starts it in all of them.
- The progress clock over a picture is steady at any frame rate.
- A group's upgrade shows its progress, whichever building is paying for it.
- Click a unit with several factories selected and it takes the shortest queue.
- A plane stays buildable while any selected airfield still has a free spot. One full airfield used to grey the button out for the whole group, even with three empty runways in the same selection.
- A general's upgrade spreads across the selected buildings instead of hitting one.
- A clicked upgrade darkens immediately and uncovers as it progresses.
- The production strip dims fresh orders the same way.
- A second worker can take over a half-built building.
- The nearest idle worker takes a new job, without yanking a busy one off.
- A worker that is building something still offers you the whole build list.
- The GLA's decoy build list toggles both ways now.
- Workers go back to collecting supplies when they finish.
- Every countdown reads real seconds and answers the game speed.
- The clock and frame-rate readout is drawn on top of everything.
- The superweapon countdowns are pictures now, not a list of names: the same strip treatment, in the top right corner, under that readout. Each one wears its own seconds and a sweeping clock, the next to land sits at the right hand end, and if you are watching more than eighteen of them the rest become a count.
- Every button's corner markings - the hotkey, the price, the time, the number queued - sit flush in the corners instead of floating a few pixels inside them, and are set a size smaller than the button's own text: four labels at full size were eating the picture they were meant to annotate.
- The messages in the top left corner - out of money, building captured, a unit lost - are set a size smaller, the same notch the superweapon countdowns were taken down by. They are notes on the battlefield, not part of the panel, and at full size a run of them was climbing over the fight they were reporting.
- A superweapon's wait is written in plain seconds - 200, not 3:20 - so it compares with every other countdown on the screen at a glance. Once it is charged the number goes away entirely and the picture itself breathes in your colour, which is the one thing you want to catch out of the corner of your eye.
- The countdown inside a strip picture moved down into its bottom left corner and carries the unit with it - 45s, 200s - on a translucent plate. In the middle of the picture it sat on the one thing the picture is there to show, and the plate it lost when it was in the middle earns its place down in the corner, where a pale cameo was swallowing the digits and their shadow together. This applies to the superweapon countdowns in the top right as well.
- Every picture in the row stands in a tray - the same tray your general's powers stand in down in the corner, your own side's copy of it, so the American, Chinese and GLA strips each carry their own metal. The tray is turned back to front, because that bar grows out of the right hand corner and the strip grows out of the left, so its heavy rail leads the row instead of standing between every pair of pictures. It is never stretched to fit, and side by side the trays close up behind each other by exactly what that bar closes its own up by, so a row of them reads as one run of metal rather than a line of loose boxes. Stacked they stand clear of each other instead: a picture in a column carries its whole tray, with none of the one above it cutting across its top edge. The strip is flush against the left edge, and a black rectangle laid over the battlefield is gone.
- The queue stands up out of the corner instead of lying across the bottom of the screen. It is a column now, five pictures high, growing upward from just above the command bar with the next thing to arrive at the bottom of it - so the one picture you actually read is always in the same place, a thumb's width above the buttons, and it does not move when six more tanks are ordered. What is left over closes the column as a sixth cell wearing a "+N". A busy base used to lay its whole queue across the battlefield; now it costs one picture's width of screen, whatever it has coming. Watching a match it goes back to rows, because there the up and down belongs to the players: one row each, piled off the bottom left corner, each row reading left to right.
- A slot in it is a slot of that bar to the pixel, at every resolution - the size is measured off the bar itself rather than guessed - so the queue is read at a glance instead of squinted at: these were postage stamps a third of that size. Ten tanks ordered back to back are one picture with an x10 in its corner rather than ten copies of the same tank eating the row, so what is left of the row still says what else is coming.
- The buildings your workers are raising stand in that same column, not in one of their own: a war factory going up is a picture in the queue at the point its time puts it, between the tank that lands before it and the one that lands after. One column is the whole answer to what your base has coming, read bottom to top in the order it arrives, and it no longer takes a second strip of screen to say the slower half of it. Each site carries its own sweep and the seconds it still needs. Click one and the camera goes there - a half-finished building under fire on the far side of the map is one click away instead of a hunt across the minimap - and none of them can be cancelled by mistake: a building already standing is sold or lost, never cancelled, so Ctrl over it does nothing.
- The superweapon strip in the top right stands in the same metal, the right way round this time: it is anchored to the right hand edge and grows leftwards, which is the way that bar in the corner grows, so its rail closes the row against the edge of the screen. The black rectangle behind those pictures is gone with it, and the countdowns are the size of the ones down in the corner rather than half of it.
- A game opens with nothing selected, so your first click is not a rally point.
- Your general's powers no longer climb the right hand edge of the screen. They sit three abreast, filling from the bottom corner where the first one has always been and wrapping upward, so a general holding eleven of them still reads as a block instead of a ladder from the radar to the sky. The keys follow the same shape: the first press picks a row, the second picks the power in it, so every power is two keys away - F1 F1 for the one in the corner, F2 F1 for the row above it. Only the keys you can press next are written on the pictures, so you are never reading eleven labels to find one, and Escape drops a row you thought better of - so does clicking a power with the mouse, and so does four seconds' pause, so a key hit by mistake in a fight never turns your next one into a superweapon. Rows with nothing in them yet cannot be picked at all: early on, when you hold three powers, only the row you have answers a key.
- The second key of that pair works now. Anything that touched the interface at all - a promotion, a unit rolling out, a building captured somewhere across the map - was quietly forgetting the row you had just picked, and in a real fight that happens several times a second, so the pair almost never completed and the powers looked as though they answered no key at all.
- The key written on a power is drawn over its cooldown sweep instead of under it. A power still charging had its label buried by the black wedge sweeping across the picture, which is exactly when you want to know which key it is.
- The tilde key opens the general's promotions, and Escape closes them - Escape used to bring up the pause menu on top of the open screen, leaving the small exit button in its corner as the only way out. The screen is five columns wide and 1 to 5 now name them: each number sits in the corner of the science that column would sell you next, and pressing it twice buys that one - once to mark, once to spend, so a point is never gone to a key hit in a hurry. Buy the top of a chain and the number drops onto the next rung down, so a whole column goes in with one finger while the fight carries on behind you.
- `S` stops your units again; the key was simply never bound.
- Every button carries its build, research or cooldown time in the corner.
- A single selected unit wears a gold bar filling toward its next rank.
- Timings everywhere: buildings, queues, superweapon charge, upgrades being researched.
- A charge bar now says how many seconds are left.
- Income per minute sits next to your money, averaged over half a minute.
- Aircraft always show how many attack runs they have left.
- The corner readout separates game time from real time, and sim rate from fps.
- Pausing stops both clocks.
- Watching a match, the strip becomes every player's queue at once: one row each, bordered in that player's colour, showing the three that land soonest and a count of the rest. Buildings going up on the ground are in those rows too, so a player answering an attack with three war factories is visible while the concrete is still wet, and eight players fit on one screen because a row is three pictures wide.
- Every row and every countdown stands in real metal, cut from the same tray the general's powers sit in down in the corner - the side your command bar is showing. Watching a match there is no bar of your own, so both strips used to fall back to a flat black box for the whole match; now they wear the metal of the player you are watching, and follow it when you switch seats.
- Watching, the bars over the buildings are everyone's too - what each factory is turning out, and how long the superweapons have left. An observer used to see none of it.
- Watching, you see the fog the player you are watching sees. The game only ever remembered one player's fog, so switching seats handed you the first player's idea of the map.
- An ally, and anyone watching, now sees a stealthed unit's muzzle flash, its promotion and the cash it earns - if they can see the unit at all. Those three asked 'is it mine' instead of 'can I see it', so allies and observers got nothing.

## Placing buildings

- The placement grid is drawn on the ground and follows the slope, and reaches twice as far as it used to.
- Unbuildable ground is a soft red wash, crossed out square by square.
- Running out of money no longer strands the ghost on the map.
- Holding shift and clicking out a row of buildings no longer stops halfway and selects one.
- That row keeps going with no worker selected, too, instead of ending after the first building.
- Turning a building no longer turns everything you build afterwards. A wheeled heading still carries from one wall to the next, but the next supply centre comes out facing the way it was designed to.
- A building you point at blocked ground slides to the nearest spot it fits, and lands there.
- The pointer keeps its build cursor while you place, even passing over your own buildings.
- GLA defences get a range ring while you site them.
- Two buildings can no longer be put down on the same spot online. A build order travels to the other players before anything appears on the ground, and every click made while it was in flight was answered by a map that still showed the square as empty - so a shift-held row on a bad connection came out as buildings standing inside each other. The ghost now knows about the orders you have already spent, and turns red over them or slides to the square beside; and the order itself is checked again where it lands, on every machine, after the first building is standing. A second one aimed at the same ground is refused there and costs you nothing.
- A click clears a half-typed building shortcut.
- The two-key building shortcut survives the command bar's redraw now.
- Every structure is those two keys and nothing else. The letter written on a cell is the second half of its pair, and pressed on its own it used to build whatever sat in the slot that letter names - so the same barracks went up either by the shortcut painted on it or by one bare key nobody had written anywhere, and a key pressed after a shortcut you had thought better of put a building on your cursor. And the pairs are read off the key bindings now instead of a list written out beside them: the second letters had been left behind when the bottom row of the command bar moved to Z X C V B N M, so half the cells answered a letter that was not on them.
- The command bar is driven by its own grid of keys - the buttons in the shape they are in, under your left hand - and there is no longer a switch for it. It shipped off by default, which meant nobody ever saw it.
- Escape cancels what you were doing before it opens the menu.

## Where the money is, and where it is coming from

- Supply piles and docks say what is left in them, in cash, over the pile. No more guessing which expansion is worth taking from the art on the model.
- Every build button carries its price in the top right corner, opposite the build time already in the other one. It was only ever in the tooltip, which means hovering one button at a time to compare two of them.
- A structure's button says what it does to the power grid, in the fourth corner: what it draws, or what a plant puts back. Money and time were already on the button and power is the third thing a base spends; it was the one figure you had to hover for, which is the wrong way round for the building you are putting up because the lights went out.
- A worker fetching or handing over a box shows a bar while it works.
- That bar is now the work itself. A worker used to reset it to empty at the very moment it finished and then walk away, so every trip looked like it left mid-handover; and after taking its last box it stood at the pile for one more full loading cycle, taking nothing. It leaves the instant the load is done - which is a little more money per trip, on every worker you own.
- Supply points refill themselves, slowly, and faster the more of a base has grown around them. A warehouse used to be a fixed lump of money that ran out and left the map with nothing to fight over: whoever mined out the middle first had the game, and the second half of every match was played on income neither side could earn any more. A box comes back every forty seconds standing on its own, every twenty with one supply centre built near it, every thirteen with two - up to what the point started with, never past it. Both sides expanding onto the same patch make it richer and then have to decide which of them keeps it.
- Workers stop hesitating on their way in and out. A dock has a queue outside it and a worker treated its place in that queue as a destination: it drove to the spot, stopped dead, waited a frame, drove to the door, stopped again, and only then went in - twice a load, and nearly every load with nothing else queued. With the dock empty it drives straight in.
- Hackers show how far off the next payout is. So do the black market and the oil derricks.
- A hacker working from inside an Internet Center shows what it earns. The green figure floats over the building on every payout, the way it does over one sitting in the field. It never appeared, because the game asked the hacker whether it could be seen and a garrisoned unit is not drawn at all - which is the one place a hacker is meant to work.
- Factories take a hundred units in the queue, not nine. The build queue only ever had nine buttons and that had become the limit.

## Health bars

- You pick who wears one. `HealthBars` in `Options.ini` takes 0 for everyone, 1 for smart, 2 for the selection only and 3 for nobody. Smart is the one worth trying: a bar over a unit at full health tells you what its absence would have told you, so healthy units go bare and anything that has been hit stays marked, with whatever you have selected or are pointing at readable either way. 2 is what the original game did. The default is 0, so nothing changes until you change it.
- A health bar keeps its size against the unit at any resolution. It was a fixed number of pixels wide and three tall, so the bigger the screen the thinner the thread over a tank.
- Health bars are in the owner's colour instead of green to red. A building going up fills its bar in that same colour as it rises, so a glance across the map says whose expansion it is - the seconds written over it already say it is not finished. A disabled building keeps the blue.
- A building's bar sits above its roof rather than inside its art.
- The progress bar stacked over the health bar is white, wherever it turns up - a factory turning out a tank, a silo charging, a supply drop counting down to its payout. It was yellow, which is a colour the game already spends on your own units and on damage, and a strip of it sitting directly above a coloured health bar read as part of the same bar.
- Every garrisonable building shows how full it is, whoever holds it.
- A vehicle's load stays private.
- What you have selected wears a white frame just outside its bar, so a selection is still readable in a crowd where every unit carries a bar - and the bar itself keeps its owner colour all the way round.
- Lamps, barrels, rocks and bushes no longer wear health bars.
- Bridges no longer float a health bar over the middle of the river. There was never anything under it: the span is part of the terrain, and the bar belonged to an invisible marker standing in the water.
- The bar is part of the unit now: click it and you select whoever it belongs to. Zoomed out, an infantryman is a few pixels of helmet under a bar that is far easier to hit, and a unit half behind a building still has its bar in the clear. It only answers clicks that would have missed everything anyway, so it never takes a click away from the unit you were actually pointing at, and right-clicking bare ground under a bar still orders a move. And it stands down the moment you have something selected: with a force in hand your clicks are aimed at the ground, and a bar hanging over that ground used to cost you the whole selection and hand you back a single passer-by.
- The city itself stops wearing them too. A building nobody owns shows a bar only if there is something to do about it - troops can go inside, or it can be taken. A row of houses does not, and neither does the concrete apron each of them stands on, which is a separate two-thousand-point object that was drawing a second bar down at street level.

## Twenty-two-year-old bugs, found by testing

- A civilian car no longer drives into your base in the first minute of every game. A map's civilians are owned by a computer player like any other, and the scouting pass looks for anything of that player's that can drive - so it was picking a parked car out of the scenery and sending it to a start position. A side with no faction and no build list has nothing to learn from the map and does not scout.
- A rifleman pulled off an oil derrick mid-capture left it flashing and chiming for the rest of the match, and the derrick changed hands anyway with nobody standing on it. Walking away now stops the capture, whether you or the computer gave the order.
- Every countdown on screen was a second too long. Rounding up a whole number of seconds gave a whole number plus one, so a ten second build said eleven - production queues, buildings going up, superweapons, all of it.
- A Chinese silo researching an upgrade while its missile charged drew both bars and both countdowns in the same row of pixels. They stack now.
- Units moving diagonally ran up to 40% faster than their own stat sheet.
- Garrisoned infantry and base defences only ever range-checked one of their weapons.
- Killing something with poison or toxin credited nobody with experience or score.
- Healing a unit counted as attacking it, so guards chased the wrong target.
- A supply centre could stop accepting trucks permanently.
- Selling a building mid-research gave no refund and left the upgrade in limbo.
- On-screen messages now hold for two and a half seconds and fade in one.
- Closing a dialog while another one sits on top of it no longer leaves the game holding a pointer to the window it just freed. Every click, every keypress and the tab order all read that list, so what it caused was a crash with no pattern to it.
- Ordering a group to use a special power that can kill the units casting it, the GLA rebel ambush over water being the one that does it every time, no longer walks a list of units that the ambush is deleting underneath.
- A replay that never recorded which seat was yours plays instead of crashing before the map loads.
- Alt-tabbing away with an arrow key held no longer leaves the camera sliding that way when you come back. The key release goes to whatever you switched to, so the game never heard it; losing focus now ends the scroll, the middle-button pan and the ALT pitch drag along with it.
- Locking Windows mid-battle no longer eats the game. Nothing that renders runs while the screen is locked, and the particle bookkeeping had been filed under rendering, so every explosion while you were away was created and never cleared up. A hundred rocket buggies firing at the ground for a minute is enough to come back to a game that has run out of memory or simply stopped responding.
- Lowercasing a long name no longer writes off the end of a 2,048 character buffer and takes the game with it. That path handles every file inside an archive and every map name, including one arriving from a host over the network, so it was reachable from outside.
- A mission script that waits for a line of speech to finish waits the same length of time on every machine. It used to ask the sound hardware how long the file was, so a player whose sound card was not working, or was turned off, got the answer zero and walked straight through waits that everyone else sat out. That is a different game on two screens, and a replay that stops matching the machine that recorded it. The length now comes out of the file itself: measured on one line of speech, a silent machine said 0 milliseconds and a machine with sound said 3,285. Both say 3,285 now.
- Laying a foundation costs less the further up the map you build. Flattening the ground under a new building scanned every row of the map from the bottom edge up to the building, threw all of them away bar the ones actually under it, and did that once per column. It works out the rows it needs now. The ground comes out identical, down to the last unit of height.
- EVA announcements that were queued behind another one no longer go quiet. A request waits its turn and expires if its turn never comes, and the sum that worked out when that was could wrap round to zero, retiring the announcement on the frame it was asked for.
- Police car lights flash at their own speed instead of your monitor's. On a fast screen with the frame cap off they were cycling five and a half times too quickly.
- Ten more places that wrote terrain, roads, bridges, trees, foundations and scorch marks into a buffer the graphics card had refused to hand over. Every one of them wrote to address zero instead, and a graphics device pulled out from under the game is all it takes; the frame is simply skipped now.
- Tank tracks no longer crash the game. The renderer has to borrow the buffer it writes them into, and when that request failed, which is what a graphics device pulled out from under the game does, the tracks were written to address zero anyway. Two more of the same shape went with it: the screenshot path and the texture size lookup both used a pointer that the failing call had never filled in. Three of those texture lookups also leaked the surface they asked about, once per procedural texture, for the life of the process.
- Double-clicking a menu button opens the screen once. The button stays live while the old screen animates out, so the second click started the same transition again and the screen opened twice, one copy left drawn over whatever came next and doing nothing sensible when clicked.
- Double-tapping 0 jumps the camera to that group.
- The mouse wheel no longer cancels a camera move already in progress.

## The options screen has pages

- Display, audio, controls, gameplay and network, behind five buttons across the top. The original screen was one panel with everything on it at once, and it was already full the day it shipped: the language filter, the keyboard button and four camera checkboxes are all still in there, parked off the right edge where nobody can reach them, because there was nowhere left to put them.
- Five settings that used to need a text editor have a control now: window mode, antialiasing and the two bloom knobs on display, and who wears a health bar on gameplay.
- Twelve more are decided for you instead of asked about. Grid placement, nudging a blocked building, 45 degree building rotation, the placement range ring, workers going back to supply, detailed build tooltips, the HUD overlay and archived replays are simply on - every one of them is the version of the game people were choosing anyway, and a page of eight checkboxes nobody unticks is a page of eight decisions nobody wanted to make. Middle-mouse panning, zoom to cursor, edge scrolling in a window and 45 degree camera steps left the screen too; those four are still yours by name in `Options.ini` if the defaults are wrong for your setup.
- A page no longer says its own name twice. Every tab used to open onto a heading repeating the word already written on the button you just pressed, with a rule under it dividing nothing from nothing.
- An open dropdown covers what is under it instead of the other way round. The list of resolutions grew downwards behind the labels below it, so half the entries were readable and half were words on top of words.
- Every button, slider and checkbox is the artwork the game already had, down to the fonts and the hover colours. The four panels the original screen used for video, audio, scrolling and network moved onto a page each without being touched inside.
- Writing a line into `Options.ini` by hand still works, and the screen shows you what is in there when it opens. Window mode takes effect the moment you accept it; antialiasing applies the next time the display device resets.
- The Defaults button still resets what it always reset, which is the original settings. The new rows keep their values.

## It fits your monitor

- The writing on screen grows with your monitor now. Every panel is stretched to your resolution and always was, but the text inside it stayed the size it was drawn at in 2003, and what growth there was stopped dead at twice - so on a 2560 wide screen a command bar three times its original size still wore eight point lettering. The keys on the build buttons, the prices and the build times were the worst of it: at 2K they were drawn, and unreadable.
- The strip of everything your base is building follows the screen too, instead of staying a row of postage stamps under a command bar three times its size.
- On a widescreen monitor the command bar is three pieces instead of one stretched strip. It was drawn for a 4:3 screen and every measurement in it is multiplied by your resolution separately across and down, so at 16:9 the whole thing came out a third wider than it is tall: square build pictures as rectangles, and one slab of metal across the entire bottom edge. The radar now sits hard in the left corner, the build grid in the middle, the selected unit's panel hard in the right corner, each at the size it was drawn at, and the battlefield shows through the two gaps that opens. At 4:3 the three pieces meet and it is the bar it has always been. The three pieces reach the screen: the paintings stop a pixel or two inside their own edges, which left a hairline of ground down both sides and along the bottom, and anything that close to an edge is now that edge. The American middle piece stands on the bottom edge like the other two sides' do, instead of hanging ten units high with a band of battlefield under the build grid, your money now sits in the middle of its readout instead of riding the top rim of it, and the build buttons are centred in the panel they are drawn on instead of pushing against its left edge with a strip of empty metal on the right. The stray pixels along the right-hand edges of the pieces are gone: a one-pixel light line down the side of the middle piece, and a lit speck sitting in the battlefield next to the bottom corner of the Chinese radar. Both were the same thing. The bar's artwork was being rescaled as it went to the graphics card, which left the last column of each picture holding a mirrored copy of it, and the last column of pixels on screen drew from there.
- The rest of the interface is drawn at the command bar's size, not the screen's width. The queue strip, the superweapon countdowns, the generals' powers bar down the right edge and the rank screen were each measured against how wide your monitor is, and the bar they sit beside is measured against the smaller of the two dimensions - so on a 16:9 screen a queue picture came out a third bigger than the build button it is a picture of, and the power slots were oblong where they were drawn square. One size for all of it now, and it is the bar's own.
- The writing on a build button keeps its size against the button. The hotkey letter, the price, the build time and the queue count were sized off how wide your monitor is while the bar they sit on is sized off the smaller of its width and its height, so the wider the screen the further the lettering grew away from the cameo underneath it. One size for both now, and it is the bar's.
- Every key the game answers to is in the options screen, and you can move any of them. The keyboard screen has been in the game since 2003 - the code for it, anyway; the layout it needed was never shipped in any data file, which is why the button that opens it was authored hidden and parked off the right edge, and why its Assign button had an empty body with a comment where the work should be. It is built now: every command in one list with the key it answers to beside it, click a row and press the key you want. A key can only mean one thing, so whatever was on it gives it up. Your changes go in `Keybinds.ini` next to `Options.ini` and hold only what you moved, so a patch that adds commands does not wipe them - and none of it is in the multiplayer checksum, so two players in one match can hold completely different keyboards.
- Minimising the command bar sends the radar and the whole middle off the bottom of the screen, and they slide back up when you bring them back. The old minimise dropped the bar a tenth of the screen and left a strip of metal, a readable money box and the top of the radar dish lying along the bottom edge, which is neither the bar nor out of the way. The selection panel stays where it is, because that is where the button that puts the rest back lives.
- The arrow on a dropdown is square at every resolution. It was drawn into a slot of a fixed 21 pixels wide however tall the box around it was stretched, so the bigger the screen the more the arrow was squeezed.
- The promotion screen closes when you press its key again, however fast you press it. The screen fades in, and the fade drives the window itself for several frames either way - so a second press during the fade read the screen as still shut and opened it again, and a press just after it was closed was undone by the fade's next frame.
- The command bar stops changing under you while you are using it. With nothing selected the bar shows one of your builders so you can place a structure without picking a dozer first, and it showed whichever one happened to be idle - so a dozer finishing a building on the far side of the base took the bar over, dropping a half-typed build hotkey and taking the structure off your cursor. The builder you are working with keeps the bar.
- The command bar's artwork moves with the command bar. Minimising it slid the buttons off the bottom of the screen and left the metal they sit on painted over the battlefield; the same painting stood still through the slide the bar makes when a match opens. It travels with the bar and goes with it.
- Widescreen resolutions are back in the options menu, and the list works during a match. It was greyed out the moment a game started, because changing it rebuilt the menus and then pushed the main menu over whatever you were playing. It rebuilds the menus for later and leaves you on the battlefield with the command bar back up.
- The rank screen is one painting instead of a stretched one. It is sized from that painting at the same scale as everything else and centred on the screen, rather than multiplied out separately across and down from the corner it was authored in - which on a widescreen monitor made it a third too wide and pushed it off centre.
- Borderless fullscreen: start with `-borderless` and the game fills the screen at your desktop resolution with no frame and no display mode change, so alt-tab is instant, nothing on the desktop gets shuffled around behind it, and a second monitor stays usable. Screen-edge scrolling works there, the way it does in fullscreen. The loading picture still opens as its own small window with your desktop around it, and the game only takes the screen once it has something to draw.
- Fullscreen, borderless and windowed are one saved setting now, so borderless no longer means adding a switch to your shortcut every time. `WindowMode` in `Options.ini` takes 0, 1 or 2 in that order; a switch on the command line still wins for that one run. Picking a different one applies when you press Accept instead of the next time you start the game: the window loses or gains its frame and the display device is rebuilt on the spot. Borderless takes your desktop resolution and nothing else, so while it is selected the resolution list greys out and the other two modes hand it straight back.
- Antialiasing is a saved setting as well: `MSAA` in `Options.ini`, 0 for off through 4 for sixteen samples. `-msaa 4` still works and still wins for that run. Whatever your card will not give is stepped down instead of refused, so asking for six samples gets you four rather than nothing.
- Picking High detail gives you high resolution textures. The setting was ignored: the resolution came from a machine benchmark that answers 'low' on anything modern, so there was no way to ask for better.
- The main menu plays its battle again instead of showing one still picture. The game asks Windows how much memory the machine has and compares the answer against a 256MB minimum, and on a machine with more than 4GB installed that answer arrived as minus one - so the better the machine, the more certain the game was that it did not meet the 2003 minimum. It switched off the animated menu, forced textures down and, at the preset detail levels, took the trees out. It reads the real figure now.
- The Default button in the options menu no longer throws away your resolution. It reset the display along with everything else, dropping you to 800x600 with no undo.
- Text does not vanish at large sizes. Any font over 100 points simply failed to load, which on a 4K screen is a missing line of interface.
- Zoom further out, with the whole map drawn instead of black corners.
- The zoom-out ceiling is the same for everyone; no setting buys you a wider view.
- The wheel covers that whole range in about six notches instead of thirty-eight.
- Zoom toward the cursor works; the spot under your pointer stays there.
- The camera stays above the ground. Zoomed in on a slope it used to end up inside the hill it was looking over, and the world opened up along the near plane.
- The far edge of the view opens with the height, instead of stopping at a distance fixed for the stock zoom - which is what put black beyond the terrain when you zoom out past it.
- The box on the radar follows the camera when you pan. It only ever redrew itself when the zoom or the angle changed, so scrolling left it behind.
- The camera turns in whole 45-degree steps, instantly, while you hold the key.
- Edge scrolling works windowed, and scroll speed no longer follows your frame rate.
- Taking over another base no longer squeezes the picture into the top four fifths of the screen.
- A skirmish opens zoomed all the way out (`StartAtMaxZoom = No` restores the old opening).
- Hold Ctrl and roll the wheel to turn a building before placing it.
- Buildings snap to the pathfinder's own grid (`GridBuildPlacement = No` for the old way).
- The grid under a structure is drawn, with blocked squares crossed out.
- Snapping now uses the true cell offset and the building's concrete apron.
- A building faces the way you drag it, read at the moment you release.
- The mouse pointer stays visible for the whole placement.

## Soldiers cast real shadows

- Infantry shadows are built from the pose: arms, head, weapon, moving with him.
- `UseShadowVolumesForSkins = No` puts the old flat blobs back.
- Scuds, rockets and falling bombs cast a shadow running along the ground.
- A big smoke cloud darkens the ground under it and fades as it does.
- The soft blob shadow for trees and scenery had never been drawn at all.
- Every one of the 128 tree types casts now, plus the bushes and palms.
- Fences, walls and props cast a real shaped shadow, worked out once.
- Each of these switches off in `GameData.ini`.

## Bright things can glow

- `Bloom = 60` in `Options.ini` makes bright things bleed light into the air.
- Off until you add that line; `BloomThreshold` sets how much of the picture joins in.
- Bloom and antialiasing work together. Turning bloom on with antialiasing on gave you a black battlefield under a live interface: the glow pass draws the world into its own picture first, and that picture came with a depth buffer nobody had cleared, so every triangle in the scene was rejected as being behind something that was not there.
- A switch you write in `Options.ini` by hand takes `yes`, `true`, `on` or `1` for on. Only the
  exact word `yes` used to count, so a line spelled any other way read as off and looked like a
  setting that did not work.
- Explosions light what is around them: 89 of them, from a tank shell to the Scud Storm,
  throw a warm flash that fades over a third of a second. A night fight used to be muzzle
  smoke over unlit ground.

## It does not crash

- Two blocks of 2003 assembly destroyed registers and took down the main menu.
- Quitting faulted twice every time; it now takes about half a second.
- A long chat message or an unusual map name could kill the process.
- Nor can a map handed to you over the network, however long a name the other machine gives it.
- An order too big for one packet arrives in pieces, and a piece that claims to belong outside the order is dropped instead of landing there.
- A chat line too long for a packet is dropped rather than arriving as a different, shorter line.
- The window no longer goes *Not Responding* during the menu's camera moves.
- Poison clouds, mine clearing, garrison kills and crew-killing weapons: all traced and fixed.
- Blowing up a full transport is survivable now, in every remaining variant.
- A unit can no longer load into a vehicle that no longer exists.
- A unit that kills itself survives its own turn now.
- Sniping an empty bike, formation moves and hacker evasion no longer crash.
- An order naming a player who is not there is dropped instead of crashing.
- An order that names one selected unit when nothing is selected is dropped too, instead of reading off the end of an empty list.
- Subdual weapons work on a unit that is being healed. Healing could drive the subdual meter below zero and it stayed there, so the next stun gun had to fill a hole before it did anything.
- A stream of projectiles remembers which ones it fired. The list was cleared into a local of the same name, so the real one started as whatever was in the recycled memory.
- A unit that has to deploy before firing does it standing still. It used to set up wherever it happened to be when the target came into range, then pack straight back up because it was still on its way somewhere.
- An Aurora that went supersonic to attack comes back down again. The timer ran out and nothing put the normal engine back.
- Calling in a gunship selects it for the player who called it, not for everyone watching the match.
- A transport told to load into something it cannot enter - or into itself, which happens when it is part of the group you gave the order to - ignores the order instead of flying over and hovering beside it.
- A pilot ejecting from a wreck no longer plays a promotion sound and animation somewhere out in the map.
- A sound finishing does not take the game with it. Every sound carries a description of itself, and that description can be gone by the time the sound ends - it is dropped when the sound is renamed, cleared by hand when what it points at is about to be deleted, and absent on a sound that was queued to repeat after a delay. The 2003 code checked for that in two places and then read straight through it in fifteen others, one of which is where every finished sound goes. It crashed mid-match with a stack that is all audio and names nothing that caused it. A sound with no description is now simply not music and not speech, which is what all fifteen questions were asking; the channel it was using is still handed back, because losing one leaks a voice for the rest of the match and enough of them go quiet.
- A guard posted at a tunnel network no longer takes the game down when its target stops existing.
  EA's own copy of that function, the one for units guarding anywhere else, tests for it on its first
  line; the tunnel copy read straight through the missing pointer and crashed mid-match.
- Alt-tabbing out no longer kills the game if something needs loading while you are away. The
  simulation keeps running while the window is not drawing, so a script can drop reinforcements in,
  and the new unit's model asks for a texture that cannot be made because there is no graphics device
  to make it on. The failure came back as a success with nothing attached, and the next line of code
  used it. Two lines apart in the log: "Are we ALT-Tabbed out?" and the crash. A load that produces
  nothing is now a failed load, which the game has always known how to handle, and the texture is
  rebuilt when the device comes back.
- A bad line in a data file says which line. The startup error for an unparseable block named the
  line the block began on, which is almost never the line that is wrong, so the answer was "one of
  these nine fields, good luck". It now also reports the line it got to and what was on it.
- A crash inside somebody else's code now says whose. Every line of the stack that is not ours used
  to read "Unknown" and a bare address, so a fault inside a graphics driver looked exactly like a
  fault inside the game. Those lines now carry the file the address belongs to and the offset into
  it, which is the difference between a report that can be acted on and one that cannot.
- The game says so in the log when the graphics device is lost, which is what happens when you
  alt-tab out. The recovery after that is the riskiest thing the renderer does, and until now it
  left no trace at all in a shipping build.
- Something going wrong now writes a readable crash report.

## Units take corners wide, and drive round a jam instead of into it

- A route used to be priced by the ground under it and nothing else, so a tank was handed the
  shortest line whether or not it could drive it. The shortest line scrapes every corner, and a
  tank that clips a corner stops, reverses and tries again while everything behind it waits. Routes
  are now charged for how close they run to terrain, and the charge climbs sharply in the last
  couple of squares, so a wide body swings out where there is room and only squeezes where there is
  no alternative.
- Squares where units are actually standing still are expensive for about a second afterwards. A
  unit that asks for a new route while stuck in a line is no longer handed the same line back: the
  queue in front of it costs something now, and a way round it that is not much longer wins.
- Routes also carry a clock. Each unit on the move says where it expects to be and roughly when,
  half a second at a time, and a route is charged where it wants a square somebody else wants at the
  same moment. Two columns following the same road are in the same squares at different moments and
  pay nothing for it; two columns crossing pay, and one of them goes round. That distinction is the
  whole point - the same map without a clock in it makes a shared road look as bad as a crossing.
- A group sent across the map plans its corridor from the member already closest to where you
  clicked, not from the one nearest the middle of the group. Planning from the middle made everyone
  ahead of it drive backwards to join, straight across the rest of the group.
- A route is now a width, not a line. Every unit used to steer at the exact middle of the ground it
  had been given, which is why twenty tanks sent across an empty field arrived in single file: they
  were all aiming at the same metre of it. Each unit now holds its own position across whatever
  width the ground allows beside it, measured where it is standing rather than fixed when the order
  was given, so an open field is crossed as a wide front and a bridge squeezes the same units back
  into a line without anybody deciding to queue. The width closes back onto the middle over the last
  few squares, so everyone still parks where you sent them.
- A group spreads into as many lanes as the road in front of it will take, and that number is now
  measured rather than assumed. It used to come from the widest reading the game can give, whatever
  the ground was: a dozen tanks on a two-lane road were handed a dozen lanes, every one of them
  wider than the road, and each was then cut back to the nearest verge - which is a column pressed
  into two files against the edges with nothing down the middle. The road is measured on both sides
  at the group's own feet when the order is given, so a wide field is crossed abreast, a lane road
  puts as many units side by side as it actually holds and the rest follow, and a route running
  along a wall lays its lanes out in the room that exists instead of into the wall.
- The position a unit takes across that width is the one it already had. A group you dragged a box
  round is spread out before it starts moving, and whoever was on the left of it stays on the left
  the whole way there. The spread is proportional, so a selection two hundred feet across arrives as
  a front rather than as two files pressed against the edges, and nothing is replaced by a formation
  nobody asked for. The unit keeps that line until it arrives; there is no slow drift back to the
  middle, which was the first version of this and was exactly what pulled a group back into single
  file a few seconds after it set off.
- A tank stuck behind a slower one now pulls out and goes past, if there is room beside the route to
  do it in. It holds the new line for a second and a half so it does not weave, and gives up a
  little speed while it crosses over. Nothing here costs a new route: the road is the same, only the
  part of it the tank drives on changes.
- Measured over twenty seven-minute four-way battles on the same twenty maps: time spent standing
  behind another unit fell 31%. Over six 4v4 battles, where every unit is on one of two fronts, it
  fell 38%, units left properly stuck fell by half, and the worst single frame of a match went from
  30.6 milliseconds to 9.9.
- A corner now costs what the vehicle takes to turn it. Every turn used to be priced the same,
  whether the thing making it pivots on the spot or needs a second and a half to swing a hull round,
  and the very first turn of a route was free: the one out of where the unit is standing at the
  moment you give the order. That is why a column ordered forward would sometimes set off by turning
  round, and why a tank was handed corners it had to stop, reverse and grind through. A turn is
  charged at what that hull actually takes now, and the first step is charged against the way the
  unit is already pointing. Twelve matches on the same twelve maps: time spent standing behind
  another unit down 38%, time spent properly wedged rather than merely queuing down from 11.3 to 0.3
  unit-frames a match, and the routing itself 20% cheaper to work out, because a route a vehicle can
  drive is a route it does not come back and ask to have redone.
- A group crossing open ground no longer arrives twenty abreast. Measuring the road is what stopped
  a dozen tanks driving down a two-lane street in single file, and with nothing capping it the same
  measurement turned an open field into a firing line wider than anything the group was walking
  into. Five lanes is the ceiling: still a front on open ground, still narrow enough to go through a
  base entrance without the whole group renumbering itself sideways at the gate.
- Attack-move spreads out the way a plain move does. It is the order that expects to be interrupted,
  and it was the one order that handed nobody a place in the formation, so a group told to fight its
  way across the map went in single file while the same group told to walk there arrived as a front.
- A group that has to find a new way round keeps its shape. Any new route used to drop a unit out of
  its formation permanently: it held its place until the first thing that made it re-plan, and drove
  down the middle of the road on its own from there on. Arriving is what ends a formation now.
- A unit stuck behind a unit that is never going to move asks for a different route. It tries both
  sides of the blockage first, and the outside of the whole pack, and where the road genuinely has no
  room it now stops steering around the problem and plans another way there. The squares the jam is
  standing on are already expensive by then, so the new route goes round it. A unit that has been
  getting nowhere for a third of a second also used to stop looking for a way past at exactly the
  moment it needed one; only the politeness stops now, and the looking carries on.
- Backing out of a wedge asks the queue behind to make room first, rather than reversing into it, and
  the unit plans a fresh route once it is out instead of driving back into the hole it just left.
- Units keep their distance from each other and not only from whoever is exactly alongside them. A
  neighbour half a body ahead and half a body over, closing, is the collision nothing used to see
  coming, and it is now worth a foot of road before it is worth a stop.
- Units no longer stop dead for traffic that was never going to hit them. Something crossing your
  path at an angle is not something sitting in your lane, and for one build it was priced as though
  it were: the brake reads how fast the obstruction is travelling along your route, which for a unit
  crossing it is nothing at all, so a jeep driving across a road brought the column on it to a
  standstill. Crossing traffic now costs a lift off the throttle, and only a unit genuinely in the
  way can ask for a stop.
- A unit turning on the spot is not a unit that is stuck. A heavy hull takes two seconds to come
  about and covers no ground doing it, which the jam detector counted as being wedged, so tanks
  halfway through a turn decided they were trapped and reversed out of a jam that did not exist.
- A unit aims at a point it can actually drive to. The steering point sits a couple of squares
  ahead and off to the side of the route, and it used to be checked against the width of the road
  where the unit is standing and the width where it is aiming, but not against the gap in between.
  Both ends being wide is not the same as the middle being wide, and the straight line between them
  went through the corner of the building the route had gone round. That is what catching on
  scenery looked like from the inside.
- Those three together, over eight battles on the same eight maps with forty units moved as one
  group every twenty seconds: time spent standing behind another unit down 7%, and no stutter added
  to the worst frames of a match. One map of the eight got worse, which is written down rather than
  averaged away.
- A unit that has stopped and wants to move is never left to it. The game now checks, once a frame
  and for every unit under orders, whether it is actually getting anywhere, and when the answer has
  been no for a second and a half it does something about it: first a fresh route from where the
  unit is standing, then telling whoever is crowding it to move and letting it push through, then
  backing the body out to open ground. If it is still stuck after all that, the whole sequence runs
  again rather than giving up. None of this waits for a collision, which is what the old machinery
  needed and what a unit stuck on scenery never produces.
- This is not a promise that units never get stuck. A unit walled in by buildings has nowhere to be
  sent, and no amount of steering makes ground that is not there. What it is is a promise that
  nothing is ignored: over twenty test battles, the longest any single unit spent wanting to move
  and not moving was 2.9 seconds in ordinary play, and 3.4 in a battle deliberately set up to jam.
  That number is measured at the end of every test run, so if it ever grows the run says so.
- Units that go back and forth on the spot are counted now, which is the first step to fixing them.
  A unit shuffling beside a building is moving on every single frame, so nothing in the game noticed
  it: the jam detector wants a unit that has stopped. Ground covered against ground gained over three
  seconds catches it instead, and about fifteen units a battle turn out to do it. Nothing is done
  about it yet on purpose. Three cures were tried and measured, and all three cost more than they
  saved: backing the unit out made everything a third worse, and forbidding it to change its mind
  for three seconds made it queue instead. The count ships so the next attempt has a number to beat.
- The plain version of the same question, asked of orders rather than of units: forty units sent
  across the map at once, checked twenty seconds later, 525 orders over eight maps. Of the units
  still carrying that order, one had not got going at all. Not one in a hundred: one. That is the
  figure this work is held to from now on, and every test run prints it along with the reason each
  stalled unit gave. Over those eight battles, no unit anywhere failed to move because no route
  could be found for it, and none was jammed against another unit for a whole twenty seconds.
- Jams in the heavy test fell 14% against the build from before all of this, and in ordinary play
  the whole of it costs nothing measurable: same number of routes planned, same time spent planning
  them.

## Long orders stopped hitching

- The coarse pathfinder pass never worked, so every long move searched the whole map.
- A 23-cell route went from 55,000 cells and 256ms to 10,000 and 25ms.
- Every wall, fence and building permanently held one of the search's scratch records.
- Every abandoned search leaked one, and abandoned searches are the normal case.
- A recycled record could still point at the last unit that used it.
- A single unit lands where you clicked instead of the nearest square's middle.
- A footprint change no longer permanently blocks the ground beside a building.
- Our own rescue for narrow routes searched 140,513 squares in 285ms; it is gone.
- The coarse route is widened along its whole length, not just at the start.
- The main search stops after 20,000 squares and hands back a partial route.
- Long turns went from 513 to 201, and the worst from 2,976ms to 243ms.
- The distance guess now prices diagonals and turns, so the search actually steers.
- The straight-line shortcut only runs from a square closer than anything tried before.
- That one change: stuttering turns 615 to 108, pathfinding 11.8s to 1.6s.
- The queue is indexed, so adding a square costs one step instead of eighty.
- Joining two connected areas is one operation instead of a full table rewrite.
- Computer players think on staggered turns instead of all on the same one.
- Only one AI squad's march across the map is planned per turn.
- Map scripts, waypoints and regions are looked up directly instead of by scanning.
- Every effect knows its own place, so a burning building finds its smoke instantly.
- Decimal-to-whole-number conversion is one instruction instead of 1999 assembly.
- The minimap's fog is painted in memory and handed over once a frame.
- Minimap dots and terrain go over in one go, not one trip per dot.
- The terrain's lowest-point sweep is read along the grain now.
- A computer player choosing where to put a building could freeze the picture for a twentieth of a second. When the spot in its plan is taken it looks outwards for a free one, and on a skirmish map that search reaches most of the way across the terrain: three and a half thousand candidate positions, each a full check of whether a building fits there, all inside a single frame. It now walks one ring of that search per frame and carries on next frame from where it stopped, so it settles on the same spot it always would have, a fraction of a second later.
- An angry mob added up its members' health every frame, and so did every other mob on the map. Seventy-three of them in one frame cost 32 milliseconds - two frames of budget spent on a health bar and a rule about clicking. Each mob now does it five times a second, and they take turns, so the cost never lands on one frame.
- Those two, together with a third that has since been taken out again with the rest of the group movement work, over four seven-minute four-way battles: frames that missed a sixtieth of a second fell from 15 to 6, frames over a thirtieth from 3 to 1, and the slowest one frame in a thousand from 13.3 milliseconds to 10.5. The average is unchanged, which is the point - none of this was traded for anything.
- Every unit on the move re-checked the entire road ahead of it, every single frame. A tank sent across the map holds one long straight leg of its route, and each frame it asked whether that whole leg - three hundred squares of it - was clear, walking every square and testing a block of twenty-five around each one. That one question was a sixth of the game's entire running time. It now looks twenty squares ahead, which is thirty frames of driving, and asks again next frame like it always did. Units also drive better for it, not worse: over four battles the time spent standing behind each other fell 10% and the number left properly stuck fell from 296 a match to 10.
- Sending a group across the map could freeze the picture for a twenty-fifth of a second. The wide corridor a group travels down is its own search, the most expensive kind the game runs, and it was the only one in the pathfinder allowed to run without a limit - a three-unit team once spent 40 milliseconds on one. It now gives up after a set amount of looking and the group falls back to individual routes, which is what happens on ground too tight for a corridor anyway.
- The movement code was timing itself thirty thousand times a frame. Reading the clock that often cost 4.6% of the whole game to measure something already counted for free.
- The queue that answers "where do I walk" was allowed to answer as many units as asked in the same instant. Ordering a group, or a fight breaking out, would drop ten questions into it at once and one frame would answer all ten. It now answers three a frame, which is still many times more than a match ever asks for on average, and the rest wait a frame. Units move better for it as well as the game running smoother: the number of routes computed in a whole battle fell by a fifth, because a unit whose answer comes one frame later usually no longer needs the second and third answer it used to ask for while stuck in traffic.
- The four together, on the same four battles: the average logic frame is a fifth shorter, the worst frame of a match went from 38.6 milliseconds to 16.0, frames over a sixtieth of a second went from 2 to none at all, and the number of frames costing more than 3 milliseconds fell from 14,105 to 3,360. In an eight-player 4v4 the average frame is a tenth shorter and the worst one went from 26.7 milliseconds to 17.9.
- Putting a building on uneven ground froze the picture for a sixteenth of a second. Levelling the dirt under a foundation asked the game to re-light every blade of grass on the map - sixty-one milliseconds, measured, for a patch a few paces wide. It now redoes the patch. Over a seven-minute four-way battle that took the worst frame of the match from 82 milliseconds down to 19, and left nothing at all above a thirtieth of a second.

## Sound, video, and getting it to start at all

- Audio is real, through the audio library the retail game ships with.
- The videos play: the intro, the sizzle reel, the mission briefings, the general portraits.
- The pointer is on screen over them. It used to appear only once the main menu did, so clicking through the logos was done blind.
- About 5,600 graphics calls are translated to a modern path, none of them touched.
- No disc, no registry keys, no retail installer â€” a normal install works.
- The startup screen is this build's own, so you can see which one you launched before the menu loads.
- The 1.04 patch content is reachable again.

---

## How this was done

- Ported leaf-first: every library compiled, tested and green before its dependents.
- 14 automated suites; most bugs above were found by tests, not by reading code.
- The game plays itself: eight computer opponents from one command line.
- An opponent can be set to Human in the skirmish screen: a base with no brain behind it.
- Its sight is yours from the first frame, and clicking any of it hands you the base itself.
- Shift-Ctrl-T does the same to any opponent, a computer one included, and walks around the table.
- Headless, a 23-minute skirmish plays out in 38 seconds, identically every run - and opens no window at all, so twenty of them in a row leave the desktop and the keyboard focus alone.
- It plays itself over a network too â€” two copies, one real connection.
- That found every multiplayer replay falsely accusing itself of desync since 2003.
- Every fix was proved by putting the bug back and watching the test fail.
- No debugger here: a crash symboliser, a sampling profiler, probes in live matches.
- Reverted and recorded: wide FOV, the whole group movement rework, tree shadows.
- The six-rung difficulty ladder is back down to three. The three extra rungs were built, played and taken out again: a player picks a level once and wants to know what it means, and six names that each moved one switch was a worse answer to that than three that each describe an opponent. The machinery underneath is the same, so the levels are still tunable in the data files, and Brutal kept the top rung's numbers rather than the old Brutal ones.
- The three-piece command bar was reverted once, for having nowhere to put the painting of the bar, and is back now that the painting has been cut into three to match. Each piece is fitted by matching it against the artwork it was cut from rather than by eye: the eyeballed fit was four percent out, which nobody sees on the metal and everybody sees on the money readout.
- The opponent's decisions are argued with a number: 20 headless matches per change, same seeds, win rate and match length before and after.
- That caught two changes that looked right and measured catastrophic - a wave that waited jammed the whole production line, and a retreat rule that counted buildings as gunfire sent every attack home. Both showed up as twenty matches with zero kills.
- Massing an army before attacking is written and measured but switched off: it needs somewhere to wait that is not the production queue.
- The whole of the group movement work is out of the game again, and that is the largest single thing this list has taken back. Lanes priced by how many other units already plan to drive over a square, a unit giving up on a queue and planning its way round, the shared corridor a selection travels down, routes priced by when somebody else will be standing on a square, spacing measured off the units you selected: all of it built, all of it measured, all of it removed. What sank it was that the self-play harness cannot see a traffic jam. A computer opponent moves five units at a time and never forms the queue a person makes by dragging a box round fifteen tanks, so thirty-three of forty-eight maps came out bit-identical over ninety-six matches and the rule fired about twice a match. Then it was asked to swing wider, and four ways of doing that were tried and thrown away - two changed nothing at all, two made units wait longer than doing nothing. Overtaking went the same way over forty matches, with the time units spend properly stuck moving in the wrong direction, which is the one column it had been rebuilt to protect. EA wrote a version of that in 2003 and switched theirs off too. The speed work underneath it stayed; the behaviour is retail's again, and this is a problem to come back to with a way of measuring it that a machine opponent cannot flatter.
- Bugs deliberately left alone are pinned by a test documenting the behaviour.
- Infantry shadows were fixed in the wrong place first, and nobody has eyeballed them yet.
- The shade under smoke vanished between builds and was rewritten from these notes.
- The missing tree shadows were found by painting them red, not by reasoning.
- Zoom toward the cursor was fixed twice and is argued from code, not watched.
- The frame was finally measured rather than guessed at: 2003's own stopwatches were switched back on for a separate measurement build, and a seven-minute four-way match with two hundred and seventy units on the map was timed scope by scope.
- That killed a plan. Spreading the shadow work over sixteen cores would have bought two percent of a frame; particles, four hundredths of one percent. Nine tenths of the time is spent handing triangles to the graphics card, which no amount of threads makes faster. The plan is written down, with the numbers, so nobody spends a fortnight rediscovering it.
- One thing did come out of it: sixteen thousand five hundred lock operations per frame, all on the same lock, every time a scrap of memory is taken or given back. That is the next thing worth chasing.
- Then the question changed from "how fast" to "how steady", and the stopwatch had to change with it: an average is exactly the number that hides a stutter. The game now keeps the shape of every frame it draws and reports the worst ones, with the name of what took the time. The building-foundation freeze above was found that way, in one run, having been in the game since 2003.
- The self-play harness can set up teams now, which it never could: every computer opponent used to fight every other one. Eight players in a free-for-all spread the fighting over the whole map, and that is not the load anybody complains about - 4v4 puts every unit on one of two fronts, which is where they bunch up. That is now one switch, and it is how the numbers above were checked at the heavy end.
- A second pass with the same tool named two more, and the log did the work each time rather than anyone guessing: the computer player's search for somewhere to build, and the angry mob's health count. A third is named and not fixed - a computer team sent across the map on its approach path spends 40 milliseconds finding the wide corridor for it, once or twice a match, and that is one search rather than a mistake repeated. It is the next one.
- Group movement was asked a third time and this time answered yes, from a different direction. The
  version that was thrown out priced squares by how many other units planned to drive over them, and
  the harness could not see it work because a computer opponent never forms the queue a person makes
  by dragging a box round fifteen tanks - thirty-three of forty-eight maps came out bit-identical.
  What went back in is priced by terrain instead, which is on every map on every frame whether or
  not anyone is queuing, and the traffic and crossing costs ride on top of that rather than carrying
  the whole change on their own. Every route in every match changes, so there is something to
  measure: twenty four-player matches, same twenty maps, time spent standing behind another unit
  31% lower; six 4v4 matches, 38% lower with half the properly-stuck frames.
- It cost search time and that had to be paid for. Adding a charge the distance estimate cannot see
  is the same mistake the pathfinder was rescued from two years ago: the search stops walking toward
  the goal and fans out instead. First measurement, twenty matches, 225,419 squares looked at per
  match against 57,596 before - four times the work. Raising the estimate to match what the charge
  adds brought it back to 90,058, and with it the units left properly stuck, from 43 a match to 8.
  What remains is 56% more searching for 31% less waiting, and on the heavy 4v4 load the worst frame
  of a match got better rather than worse, because the search work spreads over frames and a traffic
  jam does not.
- Every knob in it is one switch away from off, and the switch is in the same executable, because a
  batch of matches only argues something if both halves of it were built by the same compiler on the
  same afternoon.
- Group movement was also asked one more question and answered no, then asked it again with one piece changed and answered yes. The idea was to stop treating the route as a line and treat it as a band. The first version had every unit drift away from whichever side ahead was busiest; that was built in the movement sandbox, measured against the columns the game ships over 96 layouts one at a time at four drift speeds, and it was worse at every speed and steadily worse the harder it pushed. What was missing was a reason to move sideways at all beyond a crowd reading. The version that shipped keeps the band and throws the drift away: a unit changes its line only when something slower is actually in front of it.
- The first attempt at the band did nothing at all, and looked like it worked. Each unit was to take its line from its own sideways distance to its own route, which is zero for every unit that ever existed: a unit's route starts under its own tracks. So every one of them came out on the centre line, the band was measured correctly and nobody was ever put anywhere in it, and the only thing left moving units sideways was the overtaking rule. It only showed up on screen, twenty-five tanks still collapsing into one column with all the machinery switched on. The line now comes from the group that gave the order, which is the only thing that knows the group has a shape, and it is handed down as a fraction of the group's own width rather than in feet, so a wide selection maps onto a narrow road instead of everybody piling up against the two edges.
- Paired with that, the drift back to the middle came out. It moved every unit 0.8% of the way to the same line every frame, which over four seconds is the whole spread, and it was fighting the thing it shipped alongside. Arrival is already handled by the band closing over the last few squares.
- The charge for driving through a queue was then doubled, to the weight the sandbox this was designed in opens at, and the game got worse at exactly the thing the charge is for: twelve four-way battles, time spent standing behind another unit up 64%, five of twelve maps worse and none better, one of them from 770 unit-frames to 2,968. Search got cheaper doing it - a tenth fewer squares looked at, a sixth less time - which is the tell: the routes are being decided sooner and further away from the queue, so a unit commits to a detour before it knows whether the queue was worth avoiding. The two weights are not the same quantity either, and calling them both four is a coincidence of naming - the sandbox's traffic value grows without limit and fades, the game's is capped. It is one number to put back. Measured again after the band was widened and re-anchored, it is still the wrong number, and now for a reason worth writing down: on the twelve-match average it looks 20% better, and seed by seed it wins one map and loses four. The average is one map - a single battle that went from 4,389 unit-frames of standing still down to 1,563 - carrying eleven others in the other direction, and on one of those the units that jammed against each other and both stopped went from 7 to 129.
- The band is not free of doubt, and the self-play harness is close to blind to it. Twelve four-way battles with it on and off: time spent standing behind another unit identical to three figures, the worst single frame of a match 69ms down to 28ms, and units left properly stuck 0.8 a match up to 2.9. That last column is the one the earlier overtaking attempt was thrown out over. A computer opponent moves five units at a time and never drags a box round twenty-five tanks, so the case the band is for is the case the harness does not play - the same reason the first group-movement attempt could not be measured. Both the band and the passing are one switch away from off, and twelve matches is not a result.
- With the band finally visible, it turned out to be three tanks wide. Measured over a four-player match it came out 77 feet across, and a tank is 24 of those, so a selection of twenty-five had nowhere to go and squeezed back into a column the moment they touched. The ground is now measured twice as far to each side, and the line each unit is given is a distance rather than a share of the group: members are sorted across the direction they are travelling and spaced by the width of the largest body among them, with the count of lanes capped at what the widest band can hold and everybody else queueing behind in order. A tight blob and a wide box now leave in the same shape, and a doorway still puts them back in single file because the offsets scale with the room actually found.
- Widening it exposed a second mistake, worth 10% of the time units spend standing still: the middle of the band was the middle of the free ground rather than the route itself. On a road with a cliff one side and a field the other, a unit that had never been given a line at all slid up to seventy-five feet off its own route. The two halves of the band are now measured separately and meet on the route. Twelve four-way battles: time spent blocked down 34%, from 770 unit-frames a match to 464.
- Watching the wide band in game found three more things wrong with it, and fixing them took time spent standing still down another 30%, from 464 unit-frames a match to 324, with units left properly stuck against each other down from 11.7 a match to 2.7. A tank reaching a corner used to stop there and rotate on the spot: its line was being measured sideways to the direction it came in on, while the point it was steering at had already moved past the turn onto the next stretch, so on a sharp bend the game was aiming it at somewhere behind itself. The line is now measured against whichever stretch the steering point is actually on, and any offset that lands behind the unit is thrown away rather than driven at. A group ordered onto one spot used to arrive spread across it instead of gathering on it, because the band only finished closing at the goal itself - it now shuts thirty feet short of it, and starts closing fifty feet earlier to have room to do it in.
- The third one is why a jam stayed a jam. A unit only tried to go round something in front of it if that thing was slower than it, and it asked the question by comparing engines rather than speedometers - so a column of identical tanks nose to tail, every one of them crawling and every one of them reporting full speed, never passed anybody at all. It is the actual speed now, which is the one case the passing rule was written for and the one case it refused to fire in.
- Closing the band through corners was tried at the same time and thrown out: scaling the offset by how sharp the bend is sounds right, and it more than tripled the time units spend blocked, 32 unit-frames per 1000 to 102 over twelve battles. A route bends constantly, and a band that shuts at every bend is single file for most of its length.
- A group you send somewhere now travels as a crowd rather than a queue, behind its own switch. Each unit holds a measured line across the road instead of the middle of it, moves over for a bigger unit coming up behind, pulls round something slower in front when there is room beside it, spreads out at the back of a jam and closes up again when it clears, and slows down only for traffic it is actually catching. It applies to orders given to more than one unit, because a single vehicle repositioning inside a firefight has nobody to share a road with, and the rules cost it real time when it was included: with every unit in the game steering this way the time spent standing behind another unit came out at 108 unit-frames per 1000 rather than 29.
- Two things the crowd did that no measurement could see, both of them tanks rotating on the spot instead of driving. Moving over for someone names a line a full body away, and taking it in one frame swings the point the unit is aiming at some twenty-five feet sideways two squares in front of its tracks, which is not a turn a tank can make: it stops, rotates, and by the time it is facing the new line the unit it was moving over for has gone past. A unit now slides into its new line at a quarter of the speed it is driving, which is about twenty degrees of steering. The second was the aiming point landing behind the unit - shoved out of a queue, or carried past its own lookahead through a bend - and the tank turning round and driving at it, which is the rocking back and forth. The point is now walked forward along the route until it is genuinely in front. The self-play harness reports both of these as no change at all, to three figures across sixteen battles, which is what a fix to something only the camera can see looks like.
- Two more places where the crowd was told the right thing one frame at a time and shook itself apart doing it. Keeping out of a neighbour's way is measured every frame against whatever is nearest, and whatever is nearest changes the moment two units cross, so the push each unit felt jumped from one side to the other between frames and the tank sat there twitching. The push is now averaged over about a quarter of a second, which is longer than the swap and shorter than anything a player can see. The other is the steering itself: a tank was being handed a new heading every frame with no memory of the last one, so a hundred small corrections became a wobble down the length of a column. It now turns towards the point it wants at a fixed rate rather than snapping to it, a quarter of the difference per frame while it is cruising and two thirds when something is actually in its way, because a unit dodging a collision cannot afford a smooth turn. Under two degrees off it holds the wheel still instead of chasing the last of the error, which is the difference between a tank standing still and a tank trembling.
- The one tank in thirty that stops for good now gets itself out. A unit that has asked to move and covered no ground for two seconds is not in traffic, it is wedged: between two allies it never quite touches, in the corner of a cliff, behind the one vehicle in the column that is never going to move again. Nothing counted it as blocked, because there was no collision to count, so nothing came to its rescue and it sat there for the rest of the battle. It now measures its own progress against the speed it asked for, and the answer has two rungs. A third of a second of getting nowhere and it stops being polite: no giving way, no moving over, no braking, no spreading out, all of which were costing it the speed it needs to push through. Two seconds and it backs out, to the side and behind, and tries the road again from there. Failing that it turns round. If it is walled in on all four sides it keeps trying and asks again a second later.
- Moving over for someone and joining a road are the same manoeuvre from two sides, and the crowd now does both. A unit coming in at an angle used to be invisible until it was in front of you, at which point everybody braked; the units already on the road now see it crossing their line up to a second and a half ahead and shift a lane over instead, which is what turns a queue into a zipper. The test is a real closest approach rather than a cone, and the courtesy is only extended to traffic that would have made you brake anyway - anything looser and a column spends the whole march shuffling sideways for vehicles driving vaguely alongside it.
- Three ways of building that were measured and thrown away, one of them expensive. Slowing down by how far away the traffic in front is, rather than by whether it is being caught, costs 3518 unit-frames of standing still a battle against 1236. Slowing down behind a unit that is not moving at all costs 16593, and it is the worst kind of bug: braking short of a parked ally means the collision never happens, so nothing counts the unit as blocked and none of the machinery that would have sent it round fires. It dies politely, three feet behind a tank that is never going to move. And standing the rules down for short trips, on the theory that a four-square shuffle inside a fight is not a march, is three times worse than leaving them on: short trips are exactly where units are packed tightest.
- Light vehicles could not hold a straight line: the head of a scout shook the whole way down a road an Overlord drove dead straight. The point a unit steers at sat two cells and a body ahead, taken from whichever sample of the route it happened to be beside, and both halves of that were wrong for a fast chassis. Samples are a cell apart, so the point jumped ten feet forward each time the unit crossed one and the heading jumped with it; it is taken between samples now, off the unit's own unrounded distance along the route, and slides forward a foot at a time. The other half is that thirty feet of warning is forty frames of road for an Overlord and ten for a scout, so the light stuff was steering at something under its nose where a foot of error is a full lock. Two ways of fixing that second half were tried and both cost more than they were worth. Moving the point further out for fast vehicles cured the shake and left four times as many units stuck: a point fifty feet up the road is measured against the ground fifty feet up the road, and nobody asks about the narrow bit in between. Slowing the wheel for them instead - turning towards the new heading at a rate cut by how much road the chassis eats in half a second - was better mannered and still cost nearly four times the stuck units. Lag on the wheel and a point too far ahead fail the same way: both answer where the unit was a moment ago. The sliding point cures the shake on its own, so that is all that is in.
- Vehicles walked off the side of bridges and queued up beside the entrance to them instead of driving on. A unit driving in a crowd holds a line a set distance off the middle of the road, and the road's width is measured by asking the ground either side whether a tank fits on it. On a bridge that question was being asked of the riverbank underneath. So the road read as sixty feet wide over a deck barely wide enough for two tanks, and a line held a body width off centre walked a column into the water - and on the approach, the same line put the leading tank against the abutment rather than the entrance, with everybody behind it stopped on the bank. The width is now measured on the deck the route actually runs over, and both the bridge and sixty feet of road each side of it have no width at all: every unit drives the middle of the road across a bridge, which is what the crossing is for. Coming at the entrance from an angle used to jam anyway, and that was the rescue rule doing it. A unit that has been getting nowhere for two seconds backs out sideways and tries the road again from there, and the riverbank beside an abutment is perfectly good ground to back out onto - so the vehicle left the only queue that leads onto the bridge and then had to fight its way back into it. On a bridge and its approach it now backs straight up instead, and keeps its place.
- Infantry can walk through infantry again, and a squad arrives as a squad. Soldiers have always been able to share ground in this game, and the crowd rules had quietly taken that back: a platoon sent across a field pulled itself into a rank sixty feet wide, every man of it braking, giving way and shuffling sideways for a crowd he was supposed to walk straight into. Foot soldiers are now outside the crowd model on both sides of the question. They keep to one line and follow it, and a tank no longer brakes for a rifleman it is about to drive through.
- A tank that cannot get past goes round on whatever room there is, rather than settling in behind. Two things used to send it to the back of the queue when a foot of road either side would have let it out. It would not change lanes within a second and a half of the last time it changed lanes, which is exactly the vehicle that just gave way to somebody and is now stuck behind the next one along; and it wanted six feet of air beside the vehicle it was passing or it would not go at all. A unit that has been queueing for a third of a second now asks again regardless, and settles for a body and a half of air if that is what the road has. The comfortable pass is still tried on both sides first, so nothing squeezes past where there was room to go round properly. Units also start spreading out at the back of a jam after a third of a second instead of a full second.
- A tank going round now goes round the outside of the crowd, not into the middle of it. The pass only ever asked whether the road reached the spot beside the vehicle in front; it never asked whether anything was standing there. In a group six abreast every such spot is another tank, so the unit aimed at one, was shoved back out of it, and queued inside its own group - which is what it looked like on screen. It now knows where its neighbours sit across the road, refuses any line one of them is already on, and has the outer edge of the whole pack as a third choice between the comfortable pass and the tight one. With a single vehicle in front the outside of the pack and the tight pass beside it are the same line, so an ordinary overtake is unchanged.
- A group is now as wide as the road it is on, and it works that out the whole way rather than once. The number of lanes was decided at the moment the order was given, from two measurements taken where the selection happened to be standing - so a dozen tanks leaving a base through a gate were handed one lane and drove the next thousand feet of open ground in single file, because nothing asked the question a second time. What the order hands out now is a place in the rank: which vehicle you are from the left, out of how many, at what spacing. How many of those ranks fit is answered every frame against the ground beside that vehicle. The column closes to single file in the gate, opens to six abreast on the far side, and closes again at the next gate, and none of it is a decision anybody makes. Widening asks for half a lane of room to spare before it takes a rank, which matters more than it sounds: the raw count is a floor of a measurement that wanders, a stretch of road a hair over five lanes wide reads five, four, five, four, and every flip renumbers the whole group and sends half of it half a lane sideways and back for the length of the road. Narrowing is immediate - when the ground is gone it is gone. The ceiling on how much road can be seen either side went up with it, from a hundred feet to a hundred and sixty, which is what every other width measurement in the game already used. Measured over thirty-eight battles: units left properly stuck fell from 6.0 a match to 1.3, and time spent standing still fell 2.4% with it.
- None of the crowd was being measured, and the numbers written here for it were another rule's. A unit steers as part of a crowd only if it was handed a line across the road, a line is handed out only by the order a player gives to a selection, and a computer opponent never gives that order: it moves its teams one vehicle at a time. So every self-play batch run to argue about the crowd was driving the two rules that live in the collision code and none of the steering. The harness now has a switch that hands every army on the map one group order every twenty seconds, to alternating corners. The match it produces is nonsense and its result says nothing, but it multiplies the time units spend standing still by fourteen, from around 300 unit-frames a match to 4,200, which is the proof it is finally creating the traffic the crowd was written for.
- With the traffic there, the crowd steering does not pay for itself. Thirty-nine battles paired map by map: time spent standing behind another unit 4,444 unit-frames a match with the rules off against 4,787 with them on, 7.7% worse, eleven maps better and twenty worse. The middle map moved by 19 unit-frames either way, so that average is four bad maps carrying thirty-five that barely noticed. What it does fix is the thing it was asked to fix. Units left properly stuck fell from 3.0 a match to 1.4, and the maps that moved are the pathological ones: 62 units wedged for good down to none, 35 down to none, 7 down to none. It creates new ones elsewhere, one map going from 5 to 34. The switch stays off, and what needs explaining now is the four maps, not the average.
- Since the first attempt at the band failed silently and passed every test it had, the band can now be drawn. A switch puts each moving unit's route on the ground in blue, the line it holds as a yellow mark sideways from the unit, and the offset that survived as a green mark from the route out to where the unit is actually aiming. The three failures that look identical from the camera - a line never given out, a line given out and dropped on the way, a line applied but too small to see - are three different pictures under it, and the same run writes them to the log as well.

---

## Not there yet

- Random maps generate and can be played â€” a number picks the map and the match starts on it â€”
  but there is still no menu entry and no reroll button.
- Online and LAN play are untested.
- You need to own the game; no game data ships here.
