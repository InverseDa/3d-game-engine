# Scene Persistence Delta

## ADDED Requirements

### Requirement: Repeatably reload the active scene in Win64 Demo

The Win64 Demo SHALL accept a fresh F5 press to reload the currently published scene file, validate the candidate before publication, and continue accepting later requests in the same process after success or failure.

#### Scenario: Apply sequential authored edits
- **WHEN** the developer edits a camera or light by UUID and presses F5 after each valid edit
- **THEN** the game presents each accepted edit in order and starts navigation from the accepted author camera

#### Scenario: Reject a candidate and recover
- **WHEN** the active file contains an invalid candidate at reload and the developer later corrects it and requests reload again
- **THEN** the invalid candidate preserves the published scene and its session navigation state, and the corrected candidate can be approved and presented in the same process

### Requirement: Return machine-readable scene diagnostics

SceneTool SHALL support `check --json SCENE.json MOUNT ABSOLUTE_ASSET_ROOT` and output a single UTF-8 JSON report with success status, stable diagnostic code and available context. The check SHALL preserve the scene file.

#### Scenario: Diagnose an authored edit
- **WHEN** a script asks for JSON output on a valid scene or a scene with an invalid authored field
- **THEN** it receives one parseable report and the matching exit status, with file, field and known entity identity for an authored field error

#### Scenario: Reject a degenerate lighted mesh headlessly
- **WHEN** a scene has an active directional light and a mesh with authored normals whose composed world transform fails the normal-transform rule
- **THEN** the headless check rejects it with the corresponding diagnostic and mesh and entity IDs, using the same condition as Game preflight
