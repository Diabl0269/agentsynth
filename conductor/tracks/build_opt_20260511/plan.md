# Implementation Plan: Build Optimization and Test Restructuring

## Phases

### Phase 1: Build System Optimization
- [ ] Task: Enable ccache
    - [ ] Modify root `CMakeLists.txt` to conditionally use `ccache` via `CMAKE_CXX_COMPILER_LAUNCHER`.
- [ ] Task: Decouple Tests
    - [ ] Update root `CMakeLists.txt` to build tests only if `-DENABLE_TESTS=ON`.
    - [ ] Task: Conductor - User Manual Verification 'Build System Optimization' (Protocol in workflow.md)

### Phase 2: Test Restructuring
- [ ] Task: Define Directory Hierarchy
    - [ ] Create `Tests/Modules`, `Tests/UI`, `Tests/Engine`, `Tests/E2E` directories.
- [ ] Task: Migrate Tests
    - [ ] Move existing `*Tests.cpp` files into their new, appropriate subdirectories.
- [ ] Task: Update Test CMake
    - [ ] Adjust `Tests/CMakeLists.txt` to find and include tests from the new directory structure.
    - [ ] Task: Conductor - User Manual Verification 'Test Restructuring' (Protocol in workflow.md)

### Phase 3: Documentation Updates
- [ ] Task: Update Docs
    - [ ] Update `docs/testing.md` to reflect the new structure and `-DENABLE_TESTS=ON` workflow.
    - [ ] Update `README.md` with instructions on how to enable test builds.
    - [ ] Task: Conductor - User Manual Verification 'Documentation Updates' (Protocol in workflow.md)

### Phase 4: CI/CD Pipeline Adjustments
- [ ] Task: Update CI Workflows
    - [ ] Modify `ci.yml` in `build-and-test`, `build-and-test-asan`, `build-and-test-macos`, and `build-and-test-windows` to pass `-DENABLE_TESTS=ON`.
    - [ ] Task: Conductor - User Manual Verification 'CI/CD Pipeline Adjustments' (Protocol in workflow.md)
