# Releases

`.github/workflows/build-artifacts.yml` builds, packages and publishes every push to `main` as a
GitHub **prerelease**. `promote-release.yml` is the separate, manual step that turns one of those
into the actual release users auto-update onto. Version identity is in
[`distribution.md`](distribution.md#version-identity); the updater side is
[`auto-update.md`](auto-update.md); PR CI is [`ci-pipeline.md`](ci-pipeline.md).

## Post-merge artifact builds

After a merge to `main`, `build-artifacts.yml` triggers automatically.

A `version` job runs first: it dry-runs the same tag computation the release step below will
actually tag with, so the marketing version is known before anything builds. The build matrix then
builds and packages the app on Ubuntu, macOS and Windows — no tests, since PR CI already ran them —
using **Ninja on all three platforms** (Windows via the MSVC dev environment) so the build path
matches PR CI exactly, configuring each leg with that computed version
(`SYNTH_MARKETING_VERSION`) so it is baked into the app, plugin and installer. Finally the `release`
job tags with that exact same version, via `custom_tag`, and creates a GitHub release with all three
platform artifacts. The published tag is therefore guaranteed to equal the version every artifact
was built with.

The `release` job itself only *computes* the tag with `mathieudutour/github-tag-action@v6.2`
(`dry_run: true`, so it still returns `new_tag`/`changelog` but makes no API write); a separate
`Create release tag` bash step actually pushes it via `gh api .../git/refs`, with 3 bounded attempts
(10s/30s backoff) — FRO315: the action's own real invocation intermittently got a 403 "Resource not
accessible by integration" on that first write call, and a single-shot node action can't retry
itself.

The tag-and-release step runs **only on `push` to `main`**: a manual `workflow_dispatch` run is a
build-only dry run, useful for validating the matrix (including the Windows build) before merging.

**Docs-only and other non-code merges are skipped** via a `paths-ignore` filter: pushes that touch
only `**/*.md`, `docs/**`, `LICENSE`, `.gitignore`, `.clang-format`, `.clang-format-version`,
`.claude/**` or `mockups/**` produce no build, no version bump and no release. Mixed code-and-docs
merges release as normal.

This workflow uses ccache under a distinct `<os>-release-ccache-` key.

**Why its own key.** The Release/no-tests configuration produces different objects from `ci.yml`'s,
so the two must not share a cache scope; its `build/_deps` family is likewise kept in its own key
namespace rather than merged with `ci.yml`'s, for the GoogleTest race described in
[`ci-caching.md`](ci-caching.md#six-rules-each-learned-from-an-outage), rule 6. Its save and prune
steps carry the same `github.ref == 'refs/heads/main'` gate as `ci.yml`'s, so a
`workflow_dispatch` dry run from a branch does not write a cache under that branch's scope.

**The Windows leg's `Install NSIS` step** runs `scripts/ci-install-nsis.sh`, which installs via
Chocolatey and then explicitly resolves `makensis.exe`'s directory and appends it to
`$GITHUB_PATH`. It is not a bare `choco install nsis -y` because that had no retry, no fallback and
no ceiling: on 2026-09-16 `community.chocolatey.org` answered 503, choco "installed 0/0 packages"
and exited 0, and the whole Windows release — Tag and Release plus both appcast jobs — was skipped
on a main-branch build; a re-run passed with no code change (FRO104). The script retries choco with
a per-attempt timeout and backoff, then falls back to the NSIS installer pinned by version and
SHA-256 on SourceForge (a mismatch is fatal; it never runs an unverified installer), and judges
success by `makensis.exe` existing afterwards rather than by choco's exit status, since that 0/0 run
proved the status lies. `scripts/tests/ci-install-nsis.test.sh` covers the retry, the 0/0 case, a
hanging choco killed by the timeout, the fallback, a bad checksum and a failed download against a
fake choco in the Lint job, because a path that only runs during an outage otherwise gets tested by
the outage. Bumping the pin means changing `NSIS_VERSION` and `NSIS_SHA256` together at the top of
the script. The `$GITHUB_PATH` step is not optional plumbing:
`choco install` only updates the *machine registry* PATH, and every later step in a GitHub Actions
job is a fresh process that inherited its environment at job start — it never re-reads the registry
mid-job. Without this, `makensis` was invisible to the very next step and **every single release
build failed** ("makensis: The term ... is not recognized"). `$GITHUB_PATH` is the one channel
GitHub Actions does re-read before each step.

Fixing that unmasked a second, independent bug in `installer/windows/AgentSynth.nsi` that had never
once actually run in CI: its `OutFile` directive was `"installer\windows\AgentSynthSetup.exe"`, on
the wrong assumption that NSIS resolves a relative `OutFile` against makensis's invocation cwd. NSIS
actually resolves it against **the `.nsi` script's own directory**, already `installer\windows\`, so
the real path doubled to `installer\windows\installer\windows\AgentSynthSetup.exe` and makensis
failed with "Can't open output file." `OutFile` is now the bare filename
`"AgentSynthSetup.exe"`, which resolves to the same `installer\windows\AgentSynthSetup.exe` that
`Package Windows Artifact` and the manual verification steps expect.

## Appcast publishing

Automated in `build-artifacts.yml`:

- The macOS build job bakes in `CFBundleVersion = github.run_number` and, once the variable exists,
  the real `SUPublicEDKey`.
- After the `release` job tags and publishes the GitHub Release, `publish-appcast` downloads that
  release's macOS zip, runs Sparkle's `generate_appcast` tool against it — signing with
  `SPARKLE_PRIVATE_KEY`, with `--download-url-prefix` pointing at that same release's GitHub asset
  URLs — and uploads the resulting `appcast.xml` back onto the release as an asset.
- The Windows side mirrors this: the build job also bakes in `SYNTH_WINSPARKLE_PUBLIC_KEY`, builds
  the NSIS installer via `makensis`, and after `release`, `publish-appcast-windows` (gated on
  `vars.WINSPARKLE_PUBLIC_KEY != ''`, `runs-on: windows-latest`) signs that installer with
  `winsparkle-tool.exe`, hand-templates `appcast-windows.xml` — WinSparkle has no
  `generate_appcast`-equivalent directory scanner — and uploads it to the release.

Publishing those appcasts to `https://agentsynth.app/updates/appcast.xml` and
`.../appcast-windows.xml` — the `SUFeedURL` baked into the app (`SYNTH_UPDATE_FEED_URL` in
`CMakeLists.txt`) — is automated too, but the workflow lives in the private backend repo, not here.
It fetches `https://github.com/Diabl0269/agentsynth/releases/latest/download/appcast.xml` and
redeploys the site only when it changed. That `latest` alias only ever resolves to the newest
**non-prerelease** release, which is exactly the point: a per-push prerelease never reaches real
users' auto-update feed. A push to that repo's web app always redeploys regardless of the appcast,
and a daily schedule in the same workflow is a safety net in case a promotion's manual deploy
trigger is forgotten. The zip itself stays on GitHub Releases, since `--download-url-prefix` points
there directly and only the small `appcast.xml` needed a second home.

## Release asset upload reliability

`Create Release` only creates or updates the release object (tag, name, body, draft/prerelease); a
separate `Upload release assets` step loops over `artifacts/*` one file at a time, calling `gh
release upload "$TAG" "$f" --clobber` up to 5 times with linear backoff per file, and fails the step
if any file exhausts its retries.

**Why not the action's own `files:` input.** `softprops/action-gh-release@v1`'s asset uploader is a
single, unwrapped `fetch()` per file with no retry, so one transient GitHub error aborted the step
immediately, stranding whatever had already uploaded and skipping the appcast jobs below (both
`needs: [release]`). Two of four runs on one day published a **half-populated** prerelease that way —
a `502` for one release's `SHA256SUMS.txt`, and an HTML error page instead of JSON (`invalid json
response body ... Unexpected token '<', "<!DOCTYPE "`) for another's Windows plugins zip.

Bumping to `@v3` for retries did **not** fix it: v3's `dist/index.js` contains no
`@octokit/plugin-retry`, and the only retry layer in the bundle is an undici-level `RetryHandler`
whose default retryable methods are `GET/HEAD/OPTIONS/PUT/DELETE/TRACE` — POST, which an asset
upload is, is excluded by default. Worse, v3's `files:` path fires every asset **concurrently** via
`Promise.all` unless the action's own `preserve_order` input is set, and `Promise.all` rejects on the
*first* rejected promise without cancelling the rest: one run logged all 7 "Uploading …" lines within
1 ms of each other, then `##[error]Error creating asset temp dir` about 300 ms later, then 3 more
"Uploaded" lines — uploads already in flight kept racing to finish while the remaining 4 were
abandoned mid-request. That error string is not in v3's bundle at all, so it is GitHub's own
upload-API error surfaced raw through Octokit's `RequestError`; because `Promise.all` only surfaces
the first rejection, the log does not attribute it to a specific asset.

Sequential uploads remove the in-flight-abandonment failure mode (nothing else is racing when one
upload fails); reusing `gh release upload` for the retry means each attempt reads the file itself
rather than depending on whether a previously-consumed request body can be replayed; and the exit
code belongs to this workflow, so there is no action-internal retry or swallow behaviour left to
audit.

**Verify, do not trust the exit code.** A `Verify published release assets` step runs right after
the upload (`if: always() && steps.tag_version.outcome == 'success'`, so it still prints its diff
even if the upload step failed outright). It computes the *expected* asset set from `artifacts/` —
the same directory `Download All Artifacts` plus `Generate checksums` just populated, so it tracks
the build matrix automatically instead of a hardcoded platform list — and compares it against `gh
release view "$TAG" --json assets`, printing a missing/unexpected diff and failing the job on any
mismatch. `appcast.xml` and `appcast-windows.xml` are deliberately excluded from the comparison,
since the appcast jobs upload those *after* this job finishes.

**No published filename contains a space.** "Package Linux Artifact" copies the raw Linux executable
to `artifact/Agent.Synth`, renamed at the copy point only — the JUCE product name and build output
stay `Agent Synth` (`SYNTH_PRODUCT_NAME` in `CMakeLists.txt`). `Agent.Synth` was already the name
every past release published under, because GitHub silently rewrote the space to a period on upload,
so this is a no-op for the site's hardcoded `linuxStandalone: "Agent.Synth"` and its user-facing
download instructions. It also closes a real, separate bug: `SHA256SUMS.txt` is generated from the
*local* `artifacts/` directory (`sha256sum *`), so its `Agent Synth` line never matched the
`Agent.Synth` name the asset actually published under, and `sha256sum -c SHA256SUMS.txt` against the
downloaded file always failed. Because no published filename has a space any more, `Verify published
release assets` no longer normalizes spaces to periods before comparing — a mismatch there is a real
missing or renamed asset, not an expected GitHub rewrite.

## Promoting a build to stable

Every push to `main` ships a prerelease — continuous build and QA output, not something real users
should auto-update onto. `promote-release.yml` turns one specific already-built prerelease into the
actual release Sparkle, WinSparkle and the download page serve:

1. Pick a tag that has been running and tested: `gh release list --repo Diabl0269/agentsynth`.
2. Actions ▸ Promote Release ▸ Run workflow, with that tag (e.g. `v0.112.0`) as the input. Only the
   repo owner can run it — the job checks `github.actor`.
3. The job re-publishes the existing release as `prerelease: false`. It does **not** rebuild
   anything. It first asserts the tag is not a draft, is currently a prerelease, and carries
   `appcast.xml` and `SHA256SUMS.txt` assets — plus `appcast-windows.xml`, but only once
   `WINSPARKLE_PUBLIC_KEY` exists — and fails loudly rather than promoting a release that would leave
   auto-update or the download page's checksum link 404ing.
4. GitHub's `/releases/latest` (and `/releases/latest/download/<asset>`) now resolves to this tag.
   Trigger the private backend repo's deploy-web workflow by hand (`workflow_dispatch`) so
   `agentsynth.app/updates/appcast.xml` picks up the promoted build immediately; otherwise its daily
   schedule catches it within 24h regardless.

**How often**: whenever a batch of merged commits is worth shipping to real users. There is no fixed
cadence and deliberately no scheduled or automatic promotion — every previous release stays a
permanent prerelease, so skipping a promotion costs nothing, and auto-update only ever moves forward
on a promotion someone chose.

### Four ways a promotion silently does nothing

- **A run triggered by anyone but the owner skips the job entirely — and the run still shows
  green.** `promote-release.yml`'s `if: github.actor == 'Diabl0269'` guard is a skip, not a failure.
  Check the job's own conclusion (`skipped` vs `success`), not just that the workflow finished.
- **`DOWNLOAD_CHANNEL`, and any other build-time env var for the site, has to be set in
  `deploy-web.yml`, never in the Cloudflare Pages dashboard.** The Pages project has no Git
  integration configured — it is a direct-upload target (`wrangler pages deploy dist`), so
  Cloudflare never runs a build itself and a "build environment variable" set in its dashboard would
  just sit there unused. `astro build` only ever runs on the GitHub Actions runner.
- **A build-time variable can reach the runner and still not reach the build.** The site's
  `deploy-web.yml` sets `DOWNLOAD_CHANNEL=stable` correctly, but its Turborepo `build` task once had
  no `env` allowlist, and Turborepo's strict env mode silently hides any variable not listed there.
  The channel var never reached `astro build`, so the download page kept resolving the newest
  prerelease instead of the promoted one, with no error anywhere. A build that runs fine locally via
  a direct `pnpm --filter` invocation can still fail this way in CI, since that bypasses turbo and
  its strict env mode entirely — the only faithful local repro is the actual CI invocation through
  `turbo run`. If the live appcast shows the wrong build after step 4, check whether the value even
  reached the build before assuming the fetch is broken.
- **A promoted release does not stay the newest release for long.** Every push to `main` keeps
  minting a new prerelease regardless of what was just promoted, so the stable channel only works if
  the code actually filters for `isPrerelease === false`. Anything that falls back to "the newest
  release, prerelease or not" starts serving the next prerelease within hours of a promotion, not
  months.

`promote-release.yml` itself has no automated coverage: it is around 20 lines of `gh` CLI calls
against GitHub's own Releases API, which has no local or offline equivalent to test against — there
is no repo state to assert on, only a live API call. Verify it by running it against a real
prerelease tag and confirming that the guard rejects a run from any actor but the owner, that it
rejects a tag missing `appcast.xml`/`SHA256SUMS.txt`, and that `gh release view <tag>` shows
`isPrerelease: false` afterwards.
