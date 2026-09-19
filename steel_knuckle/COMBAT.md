Steel Knuckle now combines 3D arena footwork and limb strings with motion-command specials and a six-stock Drive gauge. It uses original move data and tuning; it is not a frame-for-frame reproduction of either series.

Player 1 keeps A/D to move, W to jump, S to crouch, Q/E to sidestep, U/I for punches, J/K for kicks, O to throw and L for Rage Art. Player 2 uses the arrow keys, comma/period, and numpad 4/5/1/2/6/3. All directions below are relative to the opponent.

| Action | Player 1 | Player 2 | Controller |
|---|---|---|---|
| Special shortcut | F | Numpad 0 | Right stick click |
| Heat Burst / Heat Smash | G | Numpad 7 | Left stick click |
| Drive Impact | H | Numpad 8 | Right trigger |
| Drive Parry | Hold V | Hold numpad 9 | Hold left trigger |
| Drive Rush | B or double forward on contact | Numpad decimal or double forward on contact | Hold left trigger, double forward |
| Drive Reversal | H during blockstun | Numpad 8 during blockstun | Right trigger during blockstun |

A neutral special shortcut fires Palm Wave. Hold down for Rising Fang, or back for Cyclone Kick. Holding an attack button while pressing the shortcut performs its Overdrive version when two Drive stocks are available.

Motion commands use numpad notation: 2 is down, 3 is down-forward, 6 is forward, 1 is down-back, and 4 is back. Finish 236 plus punch for Palm Wave, 623 plus punch for Rising Fang, or 214 plus kick for Cyclone Kick. Use both punches or both kicks to enhance the matching special. Commands must complete within 22 simulation frames, with an attack within six frames of the final direction. Hitstop preserves the command.

Confirm a jab, straight, mid kick, crouch jab, or low kick on hit or block, then enter a special during the cancel window. A whiff cannot cancel. These normals can also cancel into Drive Rush for three stocks, or into Drive Impact for one. Drive Rush from neutral costs one stock; its next cancelable normal gains four frames of hitstun and blockstun. A connecting special can cancel into a charged Rage Art.

Drive Impact absorbs two strikes during startup and its active frames. A third strike or a throw beats it. Drive Parry blocks high, mid, low and projectile attacks, but throws beat it. Its first two frames produce a Perfect Parry; follow-up damage is reduced to keep the punish in check. Releasing a parry leaves 16 frames of recovery. Drive Reversal costs two stocks and escapes blockstun with a strike-invulnerable startup, followed by a vulnerable gap and recovery.

Drive regenerates after a delay in neutral. Spending the last stock causes burnout for ten simulation seconds. Burnout disables Drive abilities, extends blockstun, permits lethal special chip, and makes a blocked corner Drive Impact cause a longer stun. Hitstop and pause freeze the resource clocks.

Heat Burst is available once per round and starts a ten-second Heat timer. Landing Power Straight against a grounded opponent activates a fifteen-second Heat Engager and closes the gap. Heat pauses during attack and hit reactions. Press Heat again for Heat Smash. During an active Heat Power Straight's contact window, forward dash, Rush, or Heat spends the remaining Heat on a Heat Dash. Heat chip becomes grey health; landing attacks or making the opponent block recovers some grey health.

Crouch-cancel a backdash to reset your stance sooner. Sidesteps can cancel into attacks after four frames and into guard after six. Jumping has grounded startup and a committed ballistic arc. Projectiles travel on a fixed line, so a sidestep or a jump can avoid them.

Back plus left kick performs Tornado Heel. It grants one extension per airborne combo. Further hits scale damage and increase gravity; repeated launchers cannot keep an opponent afloat indefinitely. Tap down-forward to low-parry. Press a punch or sidestep shortly before landing from a launch, or immediately after a knockdown, to tech roll. A tech has a brief invulnerable portion, then becomes vulnerable before returning to neutral.

Open the pause menu with Esc or P. Tab or the shoulder buttons switch pages. The Systems page explains the new controls. Left/right changes move-list pages. Training displays attack timing, contact outcomes, Drive, Heat, burnout and recoverable health; R resets the session, F5 changes the dummy and F6 shows combat volumes.

Run the simulation suite with `powershell -File steel_knuckle/tests/check.ps1`. Add `-BuildGame` to build the executable after the tests. The built game is in `build/game/fighter/steel_knuckle.exe`.

System references: [Bandai Namco's Tekken 8 guide](https://en.bandainamcoent.eu/tekken/news/tekken-8-the-guide-start-playing), [Capcom's Street Fighter 6 introduction](https://news.capcomusa.com/2022/06/02/street-fighter-6-redefines-the-genre-in-2023/).
