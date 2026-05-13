# Plan: Enable ccache for Windows CI

## Objective
Enable `ccache` for the Windows CI build job to reduce build times and ensure parity with Linux/macOS workflows.

## Key Files & Context
- `.github/workflows/ci.yml`: The CI configuration file where the cache and ccache configuration need to be added to the `build-and-test-windows` job.

## Implementation Steps

1. **Add Cache Action:** Add the `actions/cache` step to the `build-and-test-windows` job to store and restore the `ccache` directory (usually `C:\Users\runneradmin\AppData\Local\ccache` or similar on Windows).
2. **Install/Configure ccache:** Ensure `ccache` is available in the Windows environment. If not, use `choco install ccache` or equivalent.
3. **Configure CMake:** Update the `Configure CMake` step in `build-and-test-windows` to set `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER` to `ccache`.
4. **Validation:** Review the pipeline behavior in a PR to ensure the cache is hit successfully.

## Verification & Testing
- Monitor the CI logs for "ccache stats" (if added) or build performance improvement.
- Ensure the `build-and-test-windows` job completes in a comparable time to other platforms.

## Docs Updates
- Update `GEMINI.md` to note that `ccache` is now enabled on Windows if relevant for developers.
