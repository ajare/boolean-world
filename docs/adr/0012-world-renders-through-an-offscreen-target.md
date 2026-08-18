# The world renders through an offscreen target, always composited

**Status:** Accepted

Drawing the 3D world at a fraction of screen resolution is the cheapest graphical detail setting available to this game: fragment cost dominates, and the world shader is the expensive part of a frame. That requires the world to be drawn into a render texture and stretched across the screen, rather than straight to the back buffer as it was.

## Decision

The play state renders the world into an offscreen target and composites it across the whole screen with a linearly filtered fullscreen quad. A *render scale* of `full`, `half` or `quarter` selects which of three targets is used; all three are created up front, so changing the setting swaps a target rather than building one.

The offscreen pass covers the 3D scene alone. HUD text, the debug panel and ImGui are drawn after the composite, at native resolution, so no setting can make the interface harder to read than it is at full scale.

Full scale composites through a target as well, rather than bypassing it. The extra fullscreen blit costs less than the risk of a second, rarely exercised code path: the default configuration should be the one that runs constantly.

Render scale is configured under `Video` in the launcher's `Game.yaml` and crosses into the game DLL through `dllSetVideoOptions`. The F5 options panel changes it for the session only; the configuration remains the only place a value persists.

## Considered options

- **Bypass the offscreen target at full scale.** Rejected: it saves one blit on the path that needs no help, and leaves the composite untested in the configuration almost everyone runs.
- **Scale the whole frame, UI included.** Rejected: at quarter scale the debug panels and message text become unreadable, which makes the setting unusable for the debugging it exists to support.
- **Own the targets in the launcher.** Rejected: only the 3D scene is scaled, so the target must be bound around `renderScene` inside the game DLL. Launcher ownership would put a shared string name and a lifetime across the DLL boundary to no benefit.

## Consequences

- Every frame pays one fullscreen blit, including at full scale.
- The targets belong to `WorldRenderer` and are rebuilt on each map load; the setting belongs to the model and survives map transitions, so a scale chosen in play persists until the game exits.
- Anything that wants to be crisp must draw after the composite. Future world-space effects have a choice of resolution; future UI does not.
- Sub-full scales round dimensions up, so a target can exceed the screen by a pixel rather than leave one uncovered.
