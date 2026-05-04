# Games Menu And Flappy Bird Design

## Goal

Add a new `Games` submenu to the Home screen and implement a playable real-time `Flappy Bird` game for the device.

The game must feel complete in its first version:

- reachable from Home
- isolated from reader/settings behavior
- playable with the device buttons
- restartable after game over
- easy to extend later with more games

## User Flow

1. User enters Home.
2. Home menu shows a new `Games` item above `Settings`.
3. Selecting `Games` opens a dedicated games list.
4. Selecting `Flappy Bird` opens the game.
5. During play:
   - `DOWN` press triggers one upward flap impulse
   - gravity continuously pulls the bird downward
   - pipes move horizontally across the screen
   - score increments when passing pipes
6. On game over:
   - `Confirm` restarts immediately
   - `Back` returns to the games list

## Architecture

### HomeActivity

`HomeActivity` remains the entry point and adds a new menu item:

- `Browse files`
- `Recent Books`
- optional `OPDS Browser`
- `File Transfer`
- `Games`
- `Settings`

Selection logic must be updated so `Games` opens a dedicated activity instead of overloading `Settings`.

### GamesActivity

Create a dedicated `GamesActivity` for the submenu.

Responsibilities:

- render a simple list of available games
- handle menu navigation with the existing button/navigation conventions
- launch selected game activity
- return Home on `Back`

Initial contents:

- `Flappy Bird`

The activity should be structured so additional games can be appended later without redesigning the menu flow.

### FlappyBirdActivity

Create a dedicated `FlappyBirdActivity`.

Responsibilities:

- own the game state and update cadence
- process input
- render the current frame
- manage restart and exit behavior

State model:

- `Ready`
- `Running`
- `GameOver`

Behavior:

- first frame shows the playable scene immediately
- game starts on first flap input
- after collision, gameplay stops and game-over UI is shown
- `Confirm` restarts from a clean state
- `Back` exits to `GamesActivity`

## Game Mechanics

### Bird

- fixed horizontal position
- vertical position and velocity tracked as floats or fixed-point values
- flap applies an upward impulse
- gravity applies every update step

### Pipes

- generated off-screen to the right
- move left at constant speed
- each pipe pair has a vertical gap
- when a pipe pair exits the screen, recycle or respawn it with a new gap

### Scoring

- one point per pipe pair fully passed
- current score visible during play
- game-over screen shows final score

### Collision

Game ends on:

- bird hitting top boundary
- bird hitting ground
- bird intersecting a pipe body

## Rendering

Rendering stays deliberately simple for e-ink:

- black-and-white only
- geometric bird, pipes, ground, and score text
- no grayscale and no animation effects that require special panel handling

Layout:

- score at top
- centered playfield with visible top and ground boundaries
- simple ready/game-over overlays using existing text drawing primitives

## Timing Model

Use a fixed-step real-time loop driven by `millis()`.

Requirements:

- activity loop advances game simulation on elapsed time
- rendering is requested continuously while the game is active
- frame pacing should be conservative enough for the e-ink panel but still feel like a real-time game

This version does not attempt display-driver specialization. It uses the existing activity/render flow with a tuned frame cadence.

## Controls

- `DOWN`: flap
- `Confirm`:
  - no special action during normal play
  - restart after game over
- `Back`: return to games list

This keeps the control model close to the conventional Flappy Bird input pattern while adapting it to the available hardware buttons.

## Non-Goals

Not included in this change:

- persistent high scores
- difficulty settings
- sound
- multiple games beyond menu scaffolding for future expansion
- grayscale or partial-refresh rendering optimizations specific to games

## Risks

### E-ink Responsiveness

Real-time gameplay on e-ink will always be constrained by refresh speed. The implementation should bias toward stable, readable motion rather than high animation density.

### Input Feel

The `DOWN`-to-flap mapping is unconventional compared with touch/mobile Flappy Bird. The game needs tuned gravity and flap strength so the control still feels intentional and learnable.

### Activity Isolation

The game must not leak timers or state after exit. `FlappyBirdActivity` should fully reset on restart and cleanly hand control back to `GamesActivity`.

## Acceptance Criteria

- Home shows `Games` above `Settings`
- `Games` opens a dedicated game list screen
- `Flappy Bird` is selectable from that list
- Bird is controlled by `DOWN` press with flap impulse + gravity
- Pipes move, collisions work, and score increments correctly
- `Confirm` restarts after game over
- `Back` returns from game to games list
- build passes and integration does not break existing menu navigation
