# Contributing to AgentSynth

## Development Setup

1. Clone the repository
2. Build the project:
   ```bash
   cmake -S . -B build
   cmake --build build
   ```
3. Run tests:
   ```bash
   ./build/Tests/AgentSynthTests
   ```

## Pull Request Workflow

1. Create a branch from `main`
2. Make your changes
3. Ensure all tests pass locally
4. Run the linter: `clang-format -i Source/**/*.h Source/**/*.cpp`
5. Open a PR targeting `main`

## Code Style

- C++20 standard
- Follow existing code patterns
- Use `.clang-format` for formatting
- Header-only modules where practical

## Testing

- All new features should include tests
- Run `bash scripts/coverage.sh` to check coverage
- Coverage threshold: 69%

## Architecture

See [docs/architecture.md](docs/architecture.md) for project structure.
