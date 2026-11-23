# Design

## Context

`PublishScene` builds an isolated candidate World and asset session and checks the first render snapshot. Demo's one-shot replacement hook currently runs after Present. SceneTool checks JSON and cooked asset references without creating GPU resources.

## Decisions

- Win64 maps F5 into the existing per-window input snapshot. Demo consumes a fresh press in Update before the World tick and rereads the last successfully published scene path. A valid replacement resets preview navigation to its authored primary camera. Failure preserves the previous World, asset session, path and navigation state.
- Scene preparation and publication reuse the existing candidate path. Drain outstanding frames immediately before replacing the live owners. Render records the presentation outcome. Update processes the one-shot acceptance hooks and reports whether a replacement reached presentation.
- SceneTool preserves its human-facing command. `check --json SCENE.json MOUNT ABSOLUTE_ASSET_ROOT` emits one UTF-8 JSON object on stdout with result, stable code and message, plus available path, source location, entity UUID, asset UUID and field. Exit status remains 0 for valid input, 1 for invalid content and 2 for invocation errors.
- Scene builds CPU transforms from the same hierarchy rules used by restore. For meshes with authored normals under active directional lighting, SceneTool passes decoded metadata into the CPU preflight. The Core math layer provides the normal-transform check shared with WorldRenderer. Authoring validation uses existing Scene document and World math conventions. Actual drawing still validates renderer resources and snapshot inputs at runtime.

## Scope and trade-offs

The developer explicitly reloads the active JSON and uses the mounted cooked catalog. Candidate load and frame retirement are synchronous. The new path processes requests at a frame boundary, which can take longer on large scenes. File reads leave source JSON intact. Existing SceneTool and environment-driven acceptance commands retain their behavior.
