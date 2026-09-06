# FMOD Studio authoring

Open `BooleanWorld.fspro` with FMOD Studio 2.03.14. CMake stages the pinned Steam Audio 4.8.1 plug-in into this project's ignored `Plugins/` directory.

## Routing

- Route every `Theme.*` event to the `Music` bus and assign it to `Themes.bank`.
- Route every diegetic event to the `World` bus and assign it to `World.bank`.
- Never put a Steam Audio effect on `Music` or any bus in its signal path.
- Do not add a Steam Audio Mixer Return bus. Hybrid reflections are supplied through each event's Spatializer.

## Diegetic event convention

Each diegetic event must have exactly one **Steam Audio Spatializer** on its master track. Create the event without FMOD's built-in Spatializer (the default 3D panner), then add the Steam Audio Spatializer; never use both.

Configure it as follows:

- Distance Attenuation: **Curve-Driven**. Author the event's distance curve here.
- Occlusion: **Simulation-Defined**.
- Transmission: **Simulation-Defined**.
- Reflections: **enabled**.
- Pathing: **disabled**.

`event:/World/SpatializerTest` is the in-project reference configuration. It is deliberately silent: it exists to make routing and DSP conventions inspectable without adding production content.
