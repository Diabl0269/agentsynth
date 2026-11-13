# CI invariants (.github/)

Pipeline reference: [`ci-pipeline.md`](../docs/development/ci-pipeline.md), [`ci-caching.md`](../docs/development/ci-caching.md).

- **CI cache is load-bearing and fails silent**: the compiler launcher is set per language (OBJC/OBJCXX too), `ci.yml` keeps `push: main`, `build/_deps` keys on `cmake/DependencyVersions.cmake`, set `CCACHE_DIR` explicitly, PR runs restore only (main saves and prunes), stay under the 8 GiB budget. → [ci-caching](../docs/development/ci-caching.md#six-rules-each-learned-from-an-outage)
- **The file-size guard is a required Lint check**, not advisory. → [file-size-guard](../docs/development/file-size-guard.md)
- **PR titles are conventional-commit subjects** (`type(scope)!: subject`), checked by the required `pr-title.yml` "PR Title" job. → [pr-title-convention](../docs/development/pr-title-convention.md)
- **Never rename the six required check names** (`Lint`, `Build, Test, and Coverage`, `Build and Test (macOS)`, `Build and Test (Windows)`, `Docs`, `PR Title`); `ci-passthrough.yml` covers ci.yml's four on docs-only PRs. → [ci-pipeline](../docs/development/ci-pipeline.md#required-status-checks)
