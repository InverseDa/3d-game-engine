# C++ governance enforcement

`Engine/Builder/Source/Governance/CheckCppGovernance.ts` is the executable
boundary for the Phase 1.5 Core governance policy. It scans non-ThirdParty C++
under `Engine/Source`, plus actual non-ThirdParty `Build.ts` and target
configuration inputs. It is intentionally not a general naming-style linter.

## Local and CI execution

From `Engine/Builder`, run:

```text
npm run test:governance
```

The standard `npm test` script runs the synthetic governance fixtures and then
the real-tree scan. CI should call `npm test` (and the separately configured
integration test); no workflow-specific policy is hidden outside these scripts.
Diagnostics have the stable form `relative/file:line: [rule] symbol: fragment`.

## Enforced rules

- `banned-std-owner`: rejects Standard Library owning containers, strings,
  smart pointers, callbacks, optionals, streams and the filesystem-owned adapter
  types covered by the migration audit.
- `banned-glm`: rejects GLM includes, C++ namespace use, and runtime module or
  target dependencies. Comments and ordinary C++ string/raw-string literals do
  not count as code.
- `namespace-limitless`, `namespace-scope`, `global-engine-declaration`, and
  `global-engine-alias`: keep engine declarations in `LE`, the approved shallow
  semantic namespaces, or anonymous implementation namespaces. The only global
  entry points are the exact platform/test entry points recorded in the scanner.
- `public-using-namespace`: rejects `using namespace` in exported/public headers.

`LE::Detail` is approved only for implementation machinery. Public API must not
require callers to name a `LE::Detail` type; this remains part of API review
rather than a heuristic declaration-style check.

## Allowlist ownership

The allowlist in `CppGovernance.ts` is deliberately exact to repository-relative
file, enclosing symbol, and permitted owner spelling. It mirrors
`Docs/OwnedDataMigrationAudit.md`: spdlog ABI adapters in Core, three
function-local input streams, and the runtime shader compiler's function-local
filesystem adapters. A permitted spelling in another symbol in the same file is
still rejected and is covered by synthetic tests.

The Core/Launch owners of those boundaries own the allowlist entries. Remove an
entry as soon as its third-party or platform adapter no longer requires that
Standard Library type; never add a directory-wide or whole-file suppression.
