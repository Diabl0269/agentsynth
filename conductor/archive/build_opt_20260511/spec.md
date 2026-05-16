# Specification: Build Optimization and Test Restructuring

## Overview
This track focuses on improving local development iteration speed by decoupling test builds from the main target and enabling `ccache`. It also improves maintainability by restructuring the `Tests/` directory into a hierarchical layout.

## Functional Requirements
- **Build System**:
    - Enable `ccache` for local builds via `CMAKE_CXX_COMPILER_LAUNCHER`.
    - Introduce `ENABLE_TESTS` CMake option (default `OFF` for local builds, `ON` for CI).
- **Test Structure**:
    - Organize `Tests/` into hierarchical folders: `Tests/Modules`, `Tests/UI`, `Tests/Engine`, `Tests/E2E`.
    - Migrate all existing tests to the new structure.
    - Update `Tests/CMakeLists.txt` to reflect the new structure.
- **CI/CD Integration**:
    - Update all CI pipeline jobs (`.github/workflows/ci.yml`) to pass `-DENABLE_TESTS=ON` to ensure validation coverage is maintained.
- **Documentation**:
    - Update `docs/testing.md` and `README.md` to reflect the new build procedure and directory structure.

## Non-Functional Requirements
- Maintain existing test coverage (>80%).
- Ensure the CI pipeline continues to run all test suites on pull requests.
- No regressions in test functionality during restructuring.

## Acceptance Criteria
- Main build completes without triggering tests by default.
- Building with `-DENABLE_TESTS=ON` correctly triggers test builds.
- Tests are successfully organized into the defined subfolders.
- CI pipeline passes for all jobs.
- `ccache` shows cache hits on subsequent local builds.
