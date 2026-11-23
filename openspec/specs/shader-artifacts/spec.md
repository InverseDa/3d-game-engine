# shader-artifacts Specification

## Purpose

Make authored shaders reproducible build inputs and provide portable, validated shader artifacts for the staged Game and Editor outputs.

## Requirements

### Requirement: Stage shader artifacts with their target variant

The Game and Editor build SHALL compile and stage the current Vulkan shaders in the corresponding executable's `Shaders/Vulkan` directory. Each independent shader artifact SHALL track its source and transitive local includes. A failed compile SHALL preserve the previous valid artifact.

#### Scenario: Rebuild only shaders affected by a shared include
- **WHEN** a developer changes the shared BasePass constants include and rebuilds the same target
- **THEN** the shaders that consume the include are recompiled, unrelated shader outputs are retained, and a following unchanged build does no shader work

#### Scenario: Report a shader compile failure
- **WHEN** a shader input fails compilation
- **THEN** the build identifies the source and compiler failure and preserves the destination artifact

### Requirement: Start Game from staged shader artifacts

Game startup SHALL load its compiled shaders from the executable's staged artifact directory. A missing or malformed required artifact SHALL fail initialization with a diagnostic identifying that file.

#### Scenario: Render a detached sample package
- **WHEN** Game launches with its staged shaders and solid sample but without access to the source checkout or shader compiler
- **THEN** it presents the textured solid geometry and its directional shadow

#### Scenario: Reject a missing or malformed artifact
- **WHEN** a required SPIR-V file is missing or malformed
- **THEN** Game exits unsuccessfully with the affected artifact path in the startup diagnostics
