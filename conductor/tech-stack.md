# Technology Stack

## Core Technologies
- **Programming Language:** C++20
- **Framework:** JUCE 8 (managed via CMake `FetchContent`)
- **Build System:** CMake 3.15+

## Testing
- **Framework:** GoogleTest 1.14 (managed via CMake `FetchContent`)
- **Reporting:** `llvm-cov` / `llvm-profdata` (for coverage reports via `scripts/coverage.sh`)

## AI Integration
- **Provider:** Ollama (for local LLM interaction)
- **Experimental Harness:** AI integration service for patch generation.

## Dependencies & Tooling
- **Compiler Requirements:** C++20 compatible compiler (Clang, GCC, MSVC)
- **CI/CD:** GitHub Actions (.github/workflows/ci.yml, build-artifacts.yml)
- **Compiler Cache:** `ccache` (configured for build optimization)
