# Scene v1

A scene file is a UTF-8 JSON document. Change the text directly and keep it in version control. Keep entity and asset UUIDs stable when editing an existing entity or asset. To add a new entity, assign a new non-nil lowercase UUID.

```json
{
  "version": 1,
  "entities": [
    {
      "id": "20000000-0000-4000-8000-000000000001",
      "transform": {
        "translation": [0, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      }
    },
    {
      "id": "20000000-0000-4000-8000-000000000002",
      "parent": "20000000-0000-4000-8000-000000000001",
      "transform": {
        "translation": [1, 0, 0],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "mesh": {
        "mesh": "62fdc991-27a8-4a81-a077-647b47fd1f7b",
        "material": "62fdc991-27a8-4a81-a077-647b47fd1f7c",
        "debugName": "Triangle",
        "sortKey": "0"
      }
    },
    {
      "id": "20000000-0000-4000-8000-000000000003",
      "transform": {
        "translation": [0, 0, 3],
        "rotation": [0, 0, 0, 1],
        "scale": [1, 1, 1]
      },
      "camera": {
        "debugName": "Camera",
        "verticalFieldOfViewRadians": 1.05,
        "nearPlane": 0.1,
        "farPlane": 100,
        "priority": 0,
        "active": true
      }
    },
    {
      "id": "20000000-0000-4000-8000-000000000004"
    }
  ]
}
```

Top-level `version` and `entities` are required. `version` is the integer `1`. An empty `entities` array is valid. Every entity requires only `id`. To omit a component or parent, omit the entire property. A present component requires every listed field.

| Field | Value |
| --- | --- |
| `id` | Unique entity UUID for the scene. |
| `parent` | UUID of another entity in the same scene. A parent and child both need transforms, and the hierarchy must be acyclic. |
| `transform` | Local translation and scale use `[x, y, z]`. Rotation uses a nonzero quaternion `[x, y, z, w]`. Values must be finite float32 values. World normalizes the rotation when applying the transform. |
| `mesh` | `mesh` and `material` are persistent Asset IDs. `debugName` is a string. `sortKey` is a quoted decimal integer from `"0"` through `"18446744073709551615"`, without leading zeros. The string form preserves all 64 bits in editors that use double-precision JSON numbers. |
| `camera` | `debugName` is a string. `verticalFieldOfViewRadians` must be between 0 and π, exclusive. `nearPlane` is positive and `farPlane` is larger. `priority` is a signed 32-bit integer and `active` is a boolean. The active camera selected for rendering requires a valid World transform. |

The decoder accepts keys in any order and rejects unknown or duplicate keys, trailing commas, comments, malformed UTF-8, embedded NUL, invalid IDs, and unsupported versions. The v1 limits are 64 MiB per scene, 65,536 entities, and 1,024 UTF-8 bytes per decoded debug name. JSON number tokens are limited to 127 bytes. The writer uses two-space indentation, fixed field order, and ascending entity UUID order, so repeated saves of the same document are reproducible.

Scene validation checks the document and hierarchy. Applying a scene also resolves the asset references through the application's mounted catalog and validates the resulting World. The sample mesh and material in the example can be rebuilt using the commands in [AssetImport.md](AssetImport.md).
