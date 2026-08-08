# P0 owned-data migration audit (C5)

This audit is the allowlist input for C7. Outside `Engine/Source/ThirdParty`,
engine-owned dynamic data uses `LE::Array`, `LE::HashMap`, `LE::HashSet`,
`LE::String`, `LE::UniquePtr`, `LE::SharedPtr`, `LE::WeakPtr`, and
`LE::Function`. An entry below is an exact interoperability boundary, not
permission to use an STL owner for ordinary engine state.

## Exact owning Standard Library interoperability boundaries

| File / symbol | Required signature or API | Ownership and destruction |
|---|---|---|
| `Runtime/Core/Private/Logger/Log.cpp` / spdlog registry, sinks, formatter factory and `custom_flag_formatter::clone` | spdlog's public ABI requires `std::shared_ptr`, sink `std::vector`, formatter `std::unique_ptr`, and `std::string` patterns. The public Core logger facade exposes only borrowed `spdlog::logger*` and `LE::StringView`. | spdlog owns registered loggers and sinks until `LE_SHUTDOWN` calls `spdlog::shutdown`; category pointers are invalid afterwards. Formatter owners are returned to and destroyed by spdlog. No STL owner crosses the Core public API. |
| `Runtime/Launch/Private/Windows/LaunchWindows.cpp` / `TryReadShaderFile` | `std::ifstream` is the local standard stream adapter for reading a path into `LE::Array<char>`. | The stream owns the OS file handle only for the function scope and closes before the engine byte array is returned. |
| `Runtime/Launch/Private/Mac/LaunchMac.cpp` / `TryReadShaderFile` | Same local `std::ifstream` adapter as the Windows shader loader. | Same function-scoped file-handle ownership; engine bytes use `LE::Array<char>`. |
| `Runtime/Launch/Private/ShaderRuntimeCompiler.h` / `ReadBinaryFile` | `std::ifstream` is the local binary-file adapter used by runtime shader compilation. | The stream is destroyed before return; all retained bytes and text use engine owners. |
| `Runtime/Launch/Private/ShaderRuntimeCompiler.h` / `CompileHlslToSpirv` | `std::filesystem::path`, `std::error_code`, and the owning value returned by `path::string()` are required to discover/create the platform temporary directory and remove compiler outputs. | Filesystem values are function-local. `path::string()` is converted immediately to `LE::String`; output/log paths and the command are engine strings. Temporary compiler files are removed after a successful read; on failure they are intentionally retained for diagnostics. |

## Module batch results

- Core: public logger STL ownership removed; exact spdlog adapters listed above.
- RAL: public descriptions and all Vulkan temporary arrays use engine arrays.
- RFG: plans, callbacks, registries, caches, graphs, and compiler temporaries use engine owners.
- Renderer: scene/view/pipeline storage, callbacks, bridge maps, PImpls, and draw lists use engine owners.
- World: world component and extracted render-scene storage use engine arrays.
- Platform/Launch: there is no separate Platform module in this checkout; Launch-owned maps, paths, strings, and shader bytes use engine owners, with only the adapters above retained.

## Final Programs/Tests and repository scan

- No `Engine/Source/Programs` directory exists in this checkout.
- `Engine/Source/Tests/BuilderTests` does not retain a banned Standard Library
  owner. Its real migration consumers cover `Array::Resize` and embedded-NUL-safe
  String ordering in addition to the existing allocator/container/ownership tests.
- The final non-ThirdParty C++ scan covers all `*.h`, `*.hpp`, `*.cpp`, `*.cc`,
  `*.cxx`, `*.m`, and `*.mm` files under `Engine/Source`. The only banned owning
  Standard Library spellings are the Core/spdlog rows above. The only other
  Standard Library owners are the explicitly listed file/filesystem adapters.

## C5 verification record

- Builder unit tests: 10/10 passed.
- Builder integration test: 1/1 passed, including test executable exit-status behavior.
- Win64 Debug `LimitlessTests`: passed; all OOM, contract, stale-iterator,
  bounds, and explicit-failure process probes returned their documented codes.
- Win64 Release `LimitlessTests`: passed; all applicable OOM process probes
  returned 91. The shared `Core.lib` output was then rebuilt from the Debug
  intermediates before the final Game verification.
- Win64 Debug Game target: built successfully after Debug library restoration.
- Mac Game Xcode project generation: succeeded.
- `git diff --check`: no whitespace errors; Git reported only existing
  LF-to-CRLF conversion warnings for this Windows checkout.

## C7 automated enforcement

The executable policy and its exact file-plus-symbol allowlist are documented in
`Docs/CppGovernance.md`. Builder's standard unit-test script runs both synthetic
positive/negative fixtures and a zero-violation scan of the real source tree.
