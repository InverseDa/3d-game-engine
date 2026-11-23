# Renderer Directional Lighting Delta

## ADDED Requirements

### Requirement: Cast one directional shadow map

A scene rendered with shadows enabled SHALL shadow the direct directional illumination on opaque receivers using geometry in the light's view, including geometry outside the main camera frustum.

#### Scenario: Cast from outside the camera frustum
- **WHEN** the off-camera pillar blocks light to a visible point on the ground in the solid sample
- **THEN** the point is shadowed while an unoccluded reference continues to receive direct light

#### Scenario: Move a caster and navigate around the scene
- **WHEN** the pillar moves in the scene or the user changes the view
- **THEN** the shadow follows the caster geometry, and changing only the camera keeps it at the same world-space position

#### Scenario: Read submitted shadow maps safely
- **WHEN** the renderer alternates frame slots, resizes, or restores the window after minimization
- **THEN** the BasePass reads the corresponding frame's shadow map after the depth pass, and unchanged legacy sample colors remain valid
