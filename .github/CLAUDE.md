# CI invariants (.github/)

Full pipeline reference, war stories and the health-check contract live in [`docs/testing.md`](../docs/testing.md).

- **CI cache is load-bearing and fails silently** — four rules: `CMAKE_<LANG>_COMPILER_LAUNCHER` is **per language** (a language without its own launcher silently never reaches ccache; on macOS that means `OBJC`/`OBJCXX` too, or all 103 JUCE Objective-C++ units rebuild cold; `scripts/ci-cache-check.sh` audits the generated ninja files for this); `ci.yml` keeps its `push: main` trigger (caches are ref-scoped — without a main-scoped cache no PR can restore one); `build/_deps` is keyed on `cmake/DependencyVersions.cmake` only, never `CMakeLists.txt` (pin new dependencies there); `CCACHE_DIR` is set explicitly per job (defaults differ by OS and ccache version). → [`docs/testing.md`](../docs/testing.md)
