# Movement and control in Tunnel Chimera

Tunnel Chimera plays our two walks through the MIT tunnels (trips 2.1 and 2.2) with a drawing tablet: both the pictures and the sound recorded with them. Trial video and background: [tunnel-chimera.md](tunnel-chimera.md).

**The idea:** the pen doesn't point at things or move a playhead. Its movement *is* the time of the piece.
- The walks only move forward as far as the pen moves. Holding still holds everything.
- What you see and hear depends on *how* the pen moves: slow or fast, light or heavy, upright or tilted, smooth or scribbling. It depends much less on where the pen is.

---

## 1. Reading the movement

The tablet reports a few raw values:

| Raw value | Range |
|---|---|
| Position | 0–1 across the tablet |
| Pressure | 0–1 while touching |
| Tilt | left-right and forward-back, up to about 60° on this pen |
| Hover | height above the surface while in range (this tablet may not report it) |
| In range, touching, two barrel buttons | on or off |

We don't use these values directly. MayaFlux's motion channels (Kinesis `MotionChannel`, one per stream: position, pressure, tilt, hover) turn them into qualities of movement:

- **Noise is learned, then removed.** Each axis gets its own estimate of the tablet's noise, so a hand at rest reads as rest.
- **Rates of change:**
  - From position: speed and direction, acceleration, jerk (how jerky the hand is) and curvature (how tightly it turns).
  - From pressure: how fast it rises (a hard tap).
  - From tilt and hover: how fast they change.
- **The path becomes symbols.** The tablet is divided into a 4×3 grid. The program tracks which cell the pen is in and when it crosses into another. It also notices scribbling: crossing back and forth over the same line within one or two cells.

All of this is folded into one **motion amount**, from 0 (still) to 4 (very fast), which is how fast time runs. Moving the tip counts the most. Changing tilt counts too, so you can play by tilting with the tip still. Changes in pressure and hover count less. Each crossing into a new grid cell gives time a small push.

## 2. Movement becomes time

- **Pen time:** each walk advances by the motion amount, so slow strokes walk slowly, fast strokes run, and stillness stops the walk. Each walk keeps its last 6 seconds of frames, but records a frame only when it actually moves on. So "6 seconds" here means 6 seconds of *pen time*.
- **Normal time:** pressing Space switches to time running on its own, for comparison.

The picture is assembled from 12 layers (MayaFlux Chimera). Each layer is a different moment of one of the two walks, and the movement decides which moments they are:

| Layer | Which moment |
|---|---|
| Walk A, now | the present |
| Walk A, pressed | further back the harder you press, up to about 5 seconds |
| Walk A, wandering | drifts between 0.3 and 2.5 seconds back as time runs |
| Walk A, tilted | plays faster, slower or backward with left-right tilt |
| Walk A, rewinding | plays backward |
| Walk A, deep | about 5 seconds back, refreshed in jumps |
| Walk B, now | the present (comes in after 1.5 seconds) |
| Walk B, hovering | further back the higher the pen hovers |
| Walk B, tilted | plays faster, slower or backward with forward-back tilt |
| Walk B, stuttering | drifts between 0.5 and 2 seconds back, refreshed in jumps |
| Two frozen moments | one per walk, frozen when the pen touches down |

A fragment shader decides how these layers make one picture. Barrel button 1 cycles through four ways:

- **Time brush:** strokes paint the past into the present. Harder strokes reach further back, and strokes fade as time runs. Walk B shows near the pen like a ghost.
- **Tilt smear:** the picture streaks through time in the direction the pen leans.
- **Slit:** a line through the pen at the angle of the tilt, with walk A on one side and walk B on the other. The further a pixel is from the line, the older the moment it shows.
- **Mosaic:** cells around the stroke, each showing a different moment.

## 3. Movement becomes sound

Each walk's own soundtrack plays through several readers (MayaFlux `TapSet`). Each reader runs at its own speed, and speed is pitch, as on tape:

| Voice | Speed | Loudness |
|---|---|---|
| Walk A, now | how fast the pen moves | always present while moving |
| Walk A, backward | same, reversed | grows with pressure |
| Walk A, tilted | changes with left-right tilt, backward past about 30° | grows with tilt, silent when upright |
| Walk B, now | how fast the pen moves | always present while moving, quieter than A |
| Walk B, tilted | changes with forward-back tilt | grows with tilt |

So moving slowly makes the tunnels sound low and slow, and moving fast pushes them up to two octaves higher. Stopping fades everything to silence. Barrel button 2 swaps which walk leads, in both the picture and the sound.

## 4. What each movement does

| Movement | Picture | Sound |
|---|---|---|
| Hold still (pen in range) | everything holds | fades to silence |
| Slow, steady stroke | the walks move slowly; the brush paints the near past | low and slow |
| Fast stroke | time runs up to 4×; colours split along the stroke | up to two octaves higher |
| Flick | wandering layers jump to random moments | — |
| Press harder | reaches further into the past; bigger brush | walk A backward gets louder |
| Hard tap | the deep layer jumps to a new moment | — |
| Touch down | freezes that moment; fades after lifting | — |
| Tilt with the tip still | tilted layers change speed and direction; sets the angle of the smear and the slit | tilted voices bend in pitch |
| Small tight circles | the picture folds into a kaleidoscope around the pen | — |
| Scribble across a grid line | the same moment repeats (stutter); the cell turns red; colours tear | — |
| Cross into a new grid cell | time gets a small push | a small push |
| Lift out of range | the pen marker dims; everything holds | silence |

## 5. Next: friction, and tuning attention

At the moment, more movement means more time: fast, busy input is rewarded. The next version turns some of this around, after the Awareness Through Movement idea that doing less lets you sense more:

- Slow, steady, light movement builds up clarity. Rushing, jerky input and quick successions of changes dull the picture instead of feeding it.
- The calmer the hand, the more the smallest tilt matters. The motion channels already know the noise floor, so sensitivity can grow as the hand settles.
- Rest becomes part of it: after a movement, stillness settles the picture and shows what changed.
- **Tuning:** the two walks' sounds beat against each other, and the beating slows as the tilt gets closer to a target that wanders. Holding them in unison takes attention.

## Known limits

- This tablet may not report hover height. If it doesn't, the hover layer stays near the present.
- The sound doesn't yet follow the cuts, freezes or stutters, only speed, pressure and tilt.
- With everything loud at once, the sound can clip.
- In pen time the tilted voices can go very high.
- Picture and sound advance together but aren't locked to each other.
- A walk holds its last frame when the video ends; it doesn't loop.
