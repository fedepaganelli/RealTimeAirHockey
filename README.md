# Real-Time Air Hockey

A 2D Air Hockey game developed in C++ using SDL2 and POSIX Threads, as a project for the my Real-Time Embedded Systems Master's degree course.

The project focuses on real-time task scheduling, concurrent programming, synchronization mechanisms, and basic physics simulation (no hard math).

## Overview

Real-Time Air Hockey is a two-player-style air hockey game where the player competes against a bot opponent.

The game features:
- Real-time physics simulation.
- Collision detection and response between the puck and paddles.
- A computer-controlled opponent with a FSM.
- Graphical rendering and event handling through SDL2.
- Sound effects for in-game events.

## Technologies

The project is built using the following technologies:

- **C++**: used for core application logic and physics simulation.
- **SDL2**: window management, rendering, and event handling.
    - **SDL2_image**: Loads graphical assets.
    - **SDL2_mixer**: for audio playback and sound effects.
- **POSIX Threads (pthreads)**: used for concurrent execution of the physics task and the main application loop.
- **Linux**: it's the target operating system, with POSIX real-time scheduling support.

## System architecture

The application follows a two-thread architecture.

### 1. Main thread

The main thread (defined in main.cpp) is responsible for:
1) handling user input and SDL2 events.
2) updating the graphical interface.
3) rendering the game objects.
4) managing audio playback.
5) handling the main game state transitions.

The rendering loop includes an approximately 16 ms delay between iterations, targeting a frame rate of around 60 FPS.

### 2. Physics thread

The physics simulation runs in a dedicated POSIX thread.

Its main characteristics are:
1) Scheduling policy: `SCHED_FIFO`
2) Priority: 90
3) Nominal period: 10 ms
4) Clock source: `CLOCK_MONOTONIC`
5) Synchronization: POSIX mutex with priority inheritance (`PTHREAD_PRIO_INHERIT`).

The physics task uses absolute-time sleeping through `clock_nanosleep()` with `TIMER_ABSTIME`, helping prevent cumulative timing drifts.

To improve collision detection accuracy, each physics iteration is divided into 5 sub-steps of 2 ms each.

### 3. Shared state and synchronization

The two threads communicate through a shared `GameState` structure.

Access to shared data is protected by a POSIX mutex configured with the `PTHREAD_PRIO_INHERIT` protocol. This mechanism helps mitigate priority inversion.

## Physics and opponent 

### Physics simulation

The physics engine implements:
- Puck movement and velocity updates.
- Paddle movement.
- Circle-based collision detection.
- Collision response using the coefficient of restitution.
- Wall collisions and goal detection.
- Friction and artificial table drift.

The simulation uses a semi-implicit Euler integration scheme (to compute the acceleration and velocity of the system) and sub-stepping to improve numerical stability and reduce the risk of tunneling during collisions (tunneling could still happen!).

### Opponent

The computer-controlled opponent uses a finite state machine with three states:

1. **Standard:** normal defensive and offensive movement.
2. **Windup:** preparation phase before an attack.
3. **Smash:** aggressive movement towards the puck.

The opponent transitions between these states according to game conditions and internal timers.

## Project Structure

```text
RealTimeAirHockey/
├── assets/
│   ├── air_hockey_res_board.wav
│   ├── background.jpg
│   ├── bat.png
│   ├── goal.wav
│   ├── hit.wav
│   └── puck.png
│
├── CMakeLists.txt
├── config.h
├── gamestate.h
├── main.cpp
├── physics.cpp
├── physics.h
└── README.md
```

## Requirements

The project is intended to run on Linux systems.

Make sure the following dependencies are installed:

- C++ compiler
- CMake
- SDL2 development libraries
- SDL2_image development libraries
- SDL2_mixer development libraries
- POSIX Threads support

On Ubuntu, install the required packages with:

```bash
sudo apt update

sudo apt install build-essential cmake

sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev
```

## Build Instructions

Clone the repository:

```bash
git clone https://github.com/fedepaganelli/RealTimeAirHockey.git

cd RealTimeAirHockey
```

Create a build directory:

```bash
mkdir build
cd build
```

Configure the project with CMake:

```bash
cmake ..
```

Compile the application:

```bash
cmake --build . -j$(nproc)
```

## Running the Application

From the project's root directory, run:

```bash
SDL_AUDIODRIVER=pulseaudio ./build/RealTimeAirHockey
```

Running the executable from the project root ensures that the relative paths to the `assets/` directory are resolved correctly.

## Real-Time Scheduling Configuration

The physics thread uses `SCHED_FIFO` with priority 90. On Linux, assigning real-time scheduling priorities to an unprivileged process requires appropriate permissions.

One possible configuration is to grant the user a real-time priority limit through `/etc/security/limits.conf`:

```text
<USERNAME> soft rtprio 90
<USERNAME> hard rtprio 90
```

Replace `<USERNAME>` with your Linux username.

After changing the configuration, log out and log back in for the new limits to take effect.

Verify the effective limit with:

```bash
ulimit -r
```

The expected output should be:

```text
90
```

The application should then be launched as a regular user, without `sudo`.

**Note:** Real-time scheduling policies and elevated thread priorities do not, by themselves, guarantee hard real-time behavior. Meeting real-time deadlines also requires appropriate timing analysis and control of execution time, blocking, and scheduling interference (no WCET/Jitter analysis has been done on this project).