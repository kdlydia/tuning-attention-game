# Tunnel Chimera: why the pen goes through MayaFlux

Trial video: [videos/tunnel-chimera-trial-1.mp4](videos/tunnel-chimera-trial-1.mp4). This is a window recording of the first version, about 3 minutes long and without sound.

Demo with sound: [videos/tunnel-chimera-take-2.mp4](videos/tunnel-chimera-take-2.mp4). This one minute was recorded by MayaFlux itself: the window and the program's own sound.

The two tunnel walks (trips 2.1 and 2.2) are played with a drawing tablet. The circle drawn over the picture is the pen. Its legend is at the end of this page. The code isn't in this repo yet.

MayaFlux is open and programmable all the way down, but that isn't the main point here. The main point is two differences:

1. The pen is read as **motion with meaning**, not as a set of knobs.
2. The video isn't **navigated**. Its time is **composed**, on the GPU, in many layers at once.

---

## 1. A controller compared with a motion channel

**A simple controller** turns each axis into a number and each number into a parameter: x moves something, pressure sets an opacity. Each value is used the moment it arrives and then forgotten. Sensor noise goes straight through. Everything the gesture means has to be written by hand, mapping by mapping.

**A Kinesis `MotionChannel`** treats each stream (the pen's position, its pressure, its tilt, its hover height) as a signal with a history:

- **It learns the device's noise.** An `Estimate` per axis tracks that axis's own noise floor. Tablet X and Y aren't identical, so each gets its own. One model only updates the floor during calm stretches, so the floor drops as the hand settles.
- **It differentiates.** Velocity, acceleration and jerk come from the cleaned history. A spike filter (`HampelFilter`) can be set separately for each order, since acceleration and jerk amplify noise and velocity usually doesn't.
- **It knows curvature.** The curvature guard is set from the channel's own noise floor, so it works at any scale without a tuned constant.
- **It turns the path into symbols.** A `SymbolicTrajectory` reads the position through a grid (we use 4×3). It reports which cell the pen is in, how often it crosses, how long it dwells, how many different cells it visited recently, and which cell dominates. Scribbling back and forth over a line looks different from wandering, which looks different from resting.

So the program can respond to **how** the pen moves, not only **where** it is: steady or shaky, smooth or jerky, slow or rushed, dwelling or wandering, returning or repeating. Those qualities are what a gesture means, and they're what "tuning attention" needs:

- Reward slow, small, steady movement. Make the smallest tilt matter more as the hand calms.
- Don't reward fast successions of changes. Repeated or jerky input can dull the picture instead of feeding it.
- Make rest part of the piece.

None of this is possible from raw values without rebuilding these pieces first.

The channel works on any stream of numbers, vectors or 3D vectors. The same code could read the Mugic IMU, which MayaFlux receives over OSC, or a heartbeat.

| | Simple controller | Motion channel |
|---|---|---|
| Input | the current value | the value with its history |
| Noise | passes through | learned per axis, removed |
| Time | none | velocity, acceleration, jerk, curvature |
| Gesture | written by hand per mapping | cells, crossings, dwell, scribble, spread |
| Example here | pen x moves a dot | how far the walks advance depends on how the pen moves |

## 2. Navigation compared with composing time (Chimera)

**Simple navigation** is one playhead (or one camera) that the pen moves through a clip: one time, one place, forward or back.

**Chimera** builds one picture out of many layers. Each layer can be its own moment of the video:

- **Lags:** live, a fixed delay, a delay that wanders, or a delay that follows the pen. In the trial, pressure reaches up to 5 seconds back.
- **Playing freely** at any speed: forward, held still or backward. In the trial, tilt sets two of the speeds.
- **Stutter:** refreshing a layer only every few frames.
- **Entering late,** after its own delay.
- **Freezing** on a moment. Touching the pen does this in the trial.
- **Cuts** to another moment, and swapping a layer's source while it runs. A barrel button swaps the two walks.

The recent frames stay on the GPU in rings (`TextureCollection`), so this costs no copies through the CPU. What the stack of layers **means** on screen is up to a fragment shader. The trial has four:

- **Time brush:** strokes paint the past into the present.
- **Tilt smear:** time streaks along the direction you tilt.
- **Slit:** a line through the pen, with a different walk on each side and the past getting older the further you are from the line.
- **Mosaic:** cells around the stroke, each a different moment.

So the pen doesn't move along a timeline. It decides **which times are visible at once, and where**.

**Pen time.** In the trial, each walk only advances as far as the pen moves. Its ring only records new frames, so every lag is measured in pen time, and an idle pen holds the whole picture still. Space switches back to normal running time.

**Writing for the GPU directly:**
- The pen's state reaches the shader every frame as push constants.
- The recent stroke is a buffer the shader reads through its GPU address.
- Each layer has its own data the shader can read.

A new way of seeing the walks is a new shader, with no change to the engine.

## 3. Sound from the same motion

Each walk's soundtrack comes out of the video file. It's played as a `TapSet`: several readers on one stream, each with its own speed, direction and level. Speed is pitch.

The taps follow the same pen motion as the picture, so the sound scrubs like tape and goes quiet when the pen stops. A pressed-in tap plays backward, and the tilt taps bend in pitch with the lean. The trial video was recorded without sound.

## 4. Open and programmable

- **Every piece can be reached and replaced.** The motion channel exposes its estimates, filters and history. The shaders are plain GLSL. The tablet is read directly over HID and picked from a list.
- **In a typical engine** the pen arrives as values and events, so the filtering, derivatives and gesture reading would be ours to write. A video player there is one clip with one time and one speed. Showing several moments of one clip means several players and our own buffering.

---

## Legend: the pen glyph

| What the pen does | How it's drawn |
|---|---|
| Position | where the glyph is |
| In range / out of range | full brightness / dimmed at the last position |
| Touching | centre dot filled (hollow when hovering) |
| Pressure | size of the centre dot |
| Hover height | size of the ring (this tablet may not report it) |
| Tilt | ring stretched into the pen's shadow, needle pointing the way it leans |
| Speed | colour from cyan (calm) to red (fast), tail behind the movement |
| Curvature | ring breaks into more dashes on tight curves |
| Jerk | ring edge shakes |
| Motion (how fast pen time runs) | ring thickness and glow, white marker that turns only while time moves |
| Barrel buttons | two side dots, yellow when pressed |
| Grid cell | faint 4×3 grid with the pen's cell lit, red when scribbling |
| Recent stroke | dots sized by pressure, fading as time moves |

Bars at the bottom left, left to right: pressure, hover, tilt x, tilt y (from the middle), speed, curvature, motion, jerk.

Controls: barrel button 1 changes the mode, button 2 swaps the walks, Space switches between pen time and normal time, O hides the overlay, R records the window.
