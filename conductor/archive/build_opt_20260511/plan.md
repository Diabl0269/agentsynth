# Implementation Plan: Build Optimization and Test Restructuring

## Phases

### Phase 1: Build System Optimization
- [x] Task: Enable ccache
    - [x] Modify root `CMakeLists.txt` to conditionally use `ccache` via `CMAKE_CXX_COMPILER_LAUNCHER`.
- [x] Task: Decouple Tests
    - [x] Update root `CMakeLists.txt` to build tests only if `-DENABLE_TESTS=ON`.
    - [x] Task: Conductor - User Manual Verification 'Build System Optimization' (Protocol in workflow.md)

### Phase 2: Test Restructuring
- [x] Task: Define Directory Hierarchy
    - [x] Create `Tests/Modules`, `Tests/UI`, `Tests/Engine`, `Tests/E2E` directories.
- [x] Task: Migrate Tests
    - [x] Move existing `*Tests.cpp` files into their new, appropriate subdirectories.
- [x] Task: Update Test CMake
    - [x] Adjust `Tests/CMakeLists.txt` to find and include tests from the new directory structure.
    - [x] Task: Conductor - User Manual Verification 'Test Restructuring' (Protocol in workflow.md)

### Phase 3: Documentation Updates
- [x] Task: Update Docs
    - [x] Update `docs/testing.md` to reflect the new structure and `-DENABLE_TESTS=ON` workflow.
    - [x] Update `README.md` with instructions on how to enable test builds.
    - [x] Task: Conductor - User Manual Verification 'Documentation Updates' (Protocol in workflow.md)

### Phase 4: CI/CD Pipeline Adjustments
- [x] Task: Update CI Workflows
    - [x] Modify `ci.yml` in `build-and-test`, `build-and-test-asan`, `build-and-test-macos`, and `build-and-test-windows` to pass `-DENABLE_TESTS=ON`.
    - [x] Task: Conductor - User Manual Verification 'CI/CD Pipeline Adjustments' (Protocol in workflow.md)
