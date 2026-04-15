# HadesAPI

HadesAPI is a REST JSON API that lets external programs interact with games built in the Hades engine. It's designed for ML training: an agent can step the simulation forward, inject inputs, and read game state — all over HTTP.

## Quick Start

1. In the editor, open **File > Export** and check **Enable HadesAPI (--api)** for your target platform.
2. Build & Export.
3. Run the exported game in API mode:

```bash
./HadesRuntime --project . --api --api-port 7777
```

The server starts on `http://localhost:7777`. The game loop pauses and waits for commands.

## Endpoints

### GET /api/status

Health check.

```bash
curl http://localhost:7777/api/status
```

```json
{
  "status": "running",
  "engine": "Hades",
  "apiVersion": 1
}
```

### GET /api/state

Read the current game state without advancing the simulation.

```bash
curl http://localhost:7777/api/state
```

```json
{
  "observations": {
    "score": 42,
    "health": 100
  },
  "entities": [
    {
      "id": 3,
      "name": "Player",
      "position": { "x": 1.0, "y": 2.5, "z": 0.0 }
    }
  ],
  "gameOver": false
}
```

- **observations** — key-value pairs set by scripts via `HadesAPI.Observe()`.
- **entities** — all entities with a `PositionComponent3D` in the active world.
- **gameOver** — `true` if the game loop has stopped (e.g. a script error).

### POST /api/step

Advance the simulation by one or more ticks. Optionally inject key inputs before stepping.

```bash
# Advance 1 tick (default)
curl -X POST http://localhost:7777/api/step

# Advance 10 ticks
curl -X POST http://localhost:7777/api/step \
  -H "Content-Type: application/json" \
  -d '{"ticks": 10}'

# Press space (SDL keycode 32), advance 4 ticks, then release space
curl -X POST http://localhost:7777/api/step \
  -H "Content-Type: application/json" \
  -d '{
    "ticks": 4,
    "inputs": [
      {"key": 32, "action": "press"}
    ]
  }'

# Release a key
curl -X POST http://localhost:7777/api/step \
  -H "Content-Type: application/json" \
  -d '{
    "ticks": 1,
    "inputs": [
      {"key": 32, "action": "release"}
    ]
  }'
```

**Request body** (all fields optional):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ticks` | int | 1 | Number of simulation frames to advance (1–10000). |
| `dt` | float | 1/60 | Delta time per tick in seconds. |
| `inputs` | array | [] | Key events to inject before the first tick. |

Each input object:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `key` | int | required | SDL keycode (e.g. 32 = space, 119 = W). |
| `action` | string | `"press"` | `"press"` or `"release"`. |

**Response** — same format as `GET /api/state`, reflecting the state after all ticks have run.

### POST /api/reset

Reset the game to its initial state. All entity positions, components, and script instances are restored to what they were when the runtime started. Returns the fresh state.

```bash
curl -X POST http://localhost:7777/api/reset
```

```json
{
  "observations": {},
  "entities": [
    {
      "id": 3,
      "name": "Player",
      "position": { "x": 0.0, "y": 0.0, "z": 0.0 }
    }
  ],
  "gameOver": false
}
```

### POST /api/input

Queue key events without advancing the simulation. They'll be delivered on the next `/api/step`.

```bash
curl -X POST http://localhost:7777/api/input \
  -H "Content-Type: application/json" \
  -d '{
    "inputs": [
      {"key": 97, "action": "press"},
      {"key": 100, "action": "press"}
    ]
  }'
```

```json
{
  "queued": 2
}
```

## Exposing Variables from Scripts

In your C# scripts, use the `HadesAPI` class to expose values to the API:

```csharp
using Hades.Scripting;

public class GameScript : HadesScript
{
    private int score = 0;
    private float health = 100f;

    public override void OnUpdate(EntityContext context, float deltaTime)
    {
        // These values appear in the "observations" field of API responses.
        HadesAPI.Observe("score", score);
        HadesAPI.Observe("health", health);
        HadesAPI.Observe("playerX", context.Position.X);
        HadesAPI.Observe("alive", health > 0);
    }

    public override void OnKeyDown(EntityContext context, int keyCode)
    {
        if (keyCode == 32) // space
        {
            context.Position = new Vector3(
                context.Position.X,
                context.Position.Y + 5f,
                context.Position.Z);
            score++;
        }
    }
}
```

Supported types for `HadesAPI.Observe()`:

| C# Type | JSON Type | Example |
|---------|-----------|---------|
| `int` | number | `"score": 42` |
| `float` | number | `"speed": 3.14` |
| `double` | number | `"time": 1.234` |
| `bool` | boolean | `"alive": true` |
| `string` | string | `"state": "jumping"` |

Observations persist across frames. Call `HadesAPI.Clear()` to remove all observations.

## Common SDL Keycodes

| Key | Code | Key | Code |
|-----|------|-----|------|
| Space | 32 | Return | 13 |
| A | 97 | Left | 1073741904 |
| D | 100 | Right | 1073741903 |
| W | 119 | Up | 1073741906 |
| S | 115 | Down | 1073741905 |
| Escape | 27 | Tab | 9 |

Full list: [SDL_Keycode](https://wiki.libsdl.org/SDL2/SDL_Keycode)

## Python Example

A typical RL training loop:

```python
import requests

API = "http://localhost:7777"

def step(ticks=1, inputs=None):
    body = {"ticks": ticks}
    if inputs:
        body["inputs"] = inputs
    return requests.post(f"{API}/api/step", json=body).json()

def reset():
    return requests.post(f"{API}/api/reset").json()

# Training loop
for episode in range(1000):
    state = reset()

    for t in range(500):
        # Pick an action (replace with your agent's policy)
        action = [{"key": 32, "action": "press"}]

        state = step(ticks=4, inputs=action)

        score = state["observations"].get("score", 0)
        done = state["gameOver"]

        if done:
            break

    print(f"Episode {episode}: score={score}, steps={t}")
```

## CLI Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--api` | off | Enable HadesAPI mode. Implies `--headless`. |
| `--api-port` | 7777 | Port for the HTTP server. |
| `--headless` | off | Run without a window (set automatically by `--api`). |
| `--project` | required | Path to the project directory. |

## Building with API Support

HadesAPI requires the `HADES_ENABLE_API` CMake option:

```bash
cmake -S . -B build -DHADES_ENABLE_API=ON
cmake --build build --target HadesRuntime
```

When exporting from the editor, checking **Enable HadesAPI** sets this automatically.
