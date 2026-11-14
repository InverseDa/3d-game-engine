# Cooked asset format v1

`AssetFormat` defines the binary `.leasset` files consumed by the runtime. The importer rebuilds these files from version-controlled source data and stable asset identities. All lengths are byte counts. Unsigned integers use little-endian 32-bit fields, float fields use IEEE-754 binary32 in little-endian order, and UUIDs use 16 bytes in canonical UUID order. Fields are packed explicitly.

| Offset | Field |
|---|---|
| 0 | Four magic bytes `LEAS` |
| 4 | File format version, currently 1 |
| 8 | Logical asset type UUID |
| 24 | Persistent asset UUID |
| 40 | Virtual path length |
| 44 | Payload length |
| 48 | Canonical UTF-8 virtual path, with no terminator, followed immediately by payload |

Both UUIDs must be non-nil. Path bytes must already match the `FSoftAssetPath` canonical spelling and use only ASCII in v1, so catalogs can check case aliases consistently. V1 rejects path segments ending in a dot and the case-insensitive Windows stems `CON`, `PRN`, `AUX`, `NUL`, `COM1` through `COM9`, and `LPT1` through `LPT9`. Physical source and mount directories can use Unicode.

The file has no trailing padding. The complete file is capped at 128 MiB and the path at 1,024 bytes, so catalog readers can inspect at most 1,072 prefix bytes with the full file length. Header inspection borrows path bytes from the prefix and validates the declared payload length. Typed decoding checks the expected type and validates its payload separately.

| Type | Stable type UUID | Payload |
|---|---|---|
| Mesh | `a2000000-0000-4000-8000-000000000001` | Vertex count, index count, packed XYZ positions, packed zero-based indices |
| Material | `a2000000-0000-4000-8000-000000000002` | Three finite non-negative floats in RGB order |
| CPU texture | `a2000000-0000-4000-8000-000000000003` | Width, height, tightly packed RGBA8 bytes in row order |

Mesh positions must be finite. The vertex count is between 1 and 1,000,000. The index count is a positive multiple of three up to 3,000,000. Each index is less than the vertex count. The decoder derives the local axis-aligned bounding box from all positions. CPU texture dimensions each range from 1 to 4,096, with exactly four bytes per pixel and no row padding. Importing PPM supplies an opaque alpha channel.

Encode and decode preserve the caller's output on error. Decode rejects unsupported versions, malformed lengths, type mismatches and invalid payloads. Encoding the same ID, canonical path, and payload values produces identical bytes. Runtime handles and GPU resources are created after decoding.
