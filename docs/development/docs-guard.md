# Docs Guard

`scripts/check-docs.sh` checks `docs/` for CONTENT correctness, sibling to the
[file-size](file-size-guard.md) and [function-size](function-size-guard.md) guards, which check
size instead.

**Why.** A doc tree drifts silently: a file gets renamed, a section gets renumbered or deleted, a
link target moves — and nothing catches it until a reader clicks a dead link, or a stale `§N`
pointer in a `Source/**` comment sends them to the wrong place, or nowhere.

```bash
bash scripts/check-docs.sh                          # check the tree (A against baseline; B-G hard)
bash scripts/check-docs.sh --update                  # rewrite the naming baseline from the current tree
bash scripts/check-docs.sh --update --allow-growth   # ...and let a new naming entry through
bash scripts/check-docs.sh --list                    # summarize current violations in every check
bash scripts/check-docs.sh --root <dir>              # scan a different repo root (for tests)
```

## The seven checks

Every check scans the same file set: every git-tracked-**or-untracked** `*.md`, `*.cpp`, `*.h`,
`*.sh`, `*.yml`, `*.txt`, `*.json`, `*.cmake` and `*.py` anywhere under the repo root — so
`Tests/**` and `Tools/**` count exactly like `Source/**` and `docs/**` do, which matters because
`Tests/**` alone carries well over a hundred `docs/...` references. The extension list grew from
`(md cpp h sh yml)` to add `txt`/`json`/`cmake`/`py` after a stale reference survived a whole
restructure PR hidden inside a `Tools/**/Fixtures/*.json` description field, and because
`CMakeLists.txt` itself carries several doc references (a `§`-section reference among them) that no
check could previously see at all. Excluded are only generated, vendored and scratch trees
(`build*`, `.claude/`, `worktrees/`, `mockups/`, `assets/`, and `Tests/fixtures/`) — RECORDED MODEL
OUTPUT, not hand-authored, so a docs-looking string that happens to appear inside one is not ours
to fix. `Tools/TimelineOpsHarness/Fixtures/` used to be excluded on that same reasoning, but those
fixtures carry a hand-authored `description` field rather than model output, so the exclusion was
removed once it was shown to be hiding a real stale reference there. Found via `find` —
deliberately not `git ls-files`, which would silently skip a doc mid-rename that hasn't been
`git add`ed yet. An earlier verify script in this repo was bitten by exactly that gap (a
git-index-based scan missing a genuinely new, untracked file); this guard scans the working tree
directly to avoid repeating it.

`scripts/check-docs.sh` itself holds only the CLI, the file-tree scan, the naming ratchet (check A)
and its baseline I/O, and the three run modes (check/update/list) — checks B through G, and the awk
helpers they share, live in `scripts/lib/check-docs-checks.sh` (sourced, never executed directly),
split out once check G pushed the single file past this repo's own
[1,000-line cap](file-size-guard.md).

### A. Filename convention (ratcheted)

Every `docs/**/*.md` basename must be lowercase kebab-case (`README.md` is exempt). This is the
**one** ratcheted check — see [Naming ratchet](#naming-ratchet) below. It exists because the tree
predates the convention: most of `docs/` was authored `Title_Case.md`/`snake_case.md` before this
guard, so the rule is enforced on every new doc going forward without forcing a mass rename today.

### B. Markdown link targets resolve

Every `](target)` in every `*.md` whose target is a relative `.md` path (optionally `#anchor`)
must resolve to a real file, and the anchor — if present — must match a GitHub-style heading slug
(lowercase, non-alphanumeric stripped, spaces to hyphens) in that file. **Not baselined** — a
broken link is always a hard failure. A link that normalizes above this repo's root (e.g.
`../CLAUDE.md` from the workspace-root map) is skipped: a repo-scoped checker can't verify a path
outside its own tree, and whether it resolves depends on where the repo happens to be checked out.

### C. `docs/...` path mentions resolve

Catches a rename that missed a `Source/**` comment, a `CLAUDE.md` line, or another doc's prose —
not just markdown link syntax. **Not baselined.** The pattern requires the character immediately
before `docs/` to be neither `/` nor alphanumeric (start-of-string, whitespace, or punctuation
only), so a qualified path like `synth-platform/docs/foo.md` never gets misread as pointing into
*this* repo's `docs/` tree — the backend repo is private and has its own docs tree (a billing doc,
a local-development doc, etc.) that this repo's prose refers to only in generic terms, never by
literal path.

### D. `§`-section references resolve

`docs/<path>.md` followed by up to 12 characters then `§<N>`/`§<N.M>`/`§<N.M.K>` must name a
section that actually exists in that doc — a heading `## 5.3 Foo` satisfies both `§5.3` and the
coarser `§5`. **Not baselined — zero tolerance.** Unlike check A, there is no grandfathering here:
a stale section reference is actively misleading (it sends a reader to the wrong place, or
nowhere), so it is fixed at the point it goes stale, not parked for later cleanup. The 38 stale
references that had accumulated across `Source/**`, `docs/**` and `Tests/**` before this rule went
zero-tolerance were each fixed by deriving the destination from git history — which commit split or
renumbered the doc, and what the section was called before and after — never by guessing.

### E. `docs/README.md` map completeness

Every `docs/**/*.md` file except `README.md` itself must be linked at least once from
[`docs/README.md`](../README.md), and every link `docs/README.md` makes into `docs/` must resolve to
a real file. **Not baselined** — the map is either complete or it isn't. `docs/README.md` is the
map referenced throughout this repo's `CLAUDE.md` files (root and per-directory); a doc that
exists but isn't linked from it is invisible to anyone reading the map instead of grepping the
directory, and this check is what keeps that map trustworthy without relying on every PR author to
remember it by hand.

### F. `docs/...#anchor` mentions resolve outside markdown link syntax too

Check B only validates an anchor when it's written as genuine markdown link syntax — square-bracket
link text immediately followed by a parenthesized target ending in `.md`, optionally `#anchor` —
in a `*.md` file. A `docs/<path>.md#<slug>` mention written any other way (plain
prose in a `*.md` file, or anywhere in a `*.cpp`/`*.h`/`*.sh`/`*.yml`/`*.txt`/`*.json`/`*.cmake`/
`*.py` file — a comment naming a doc section, or a hand-authored fixture's description field, for
instance) was invisible to every check before this one: check C confirms the *doc* named exists, but
never looks at an anchor tacked onto it. **Not baselined — zero tolerance**, same as B/C/D/E.

**Why it exists.** The docs restructure makes every heading unnumbered and converts the roughly 500
existing `§N` references — hard-gated by check D, zero tolerance — into `#anchor` references.
Without check F, that restructure would trade 500 gated references for ungated ones, exactly the rot
check D exists to stop.

Check F does not reimplement anything check B or check C already got right:

- **Slug rules** — it validates against the exact same slug table check B builds (GitHub-style:
  lowercase, non-alphanumeric stripped, spaces to hyphens), built once per run and shared by both
  checks, so they can never disagree about what a valid slug is.
- **Boundary rule** — it reuses check C's rule that the character immediately before `docs/` must
  be neither `/` nor alphanumeric, so a qualified sibling-repo path like
  `synth-platform/docs/billing.md#some-section` is never misread as naming *this* repo's `docs/`
  tree, anchor and all.
- **No double-reporting** — a `docs/<path>.md#<anchor>` mention where `<path>.md` doesn't exist at
  all is check C's failure to report, not check F's; check F skips it rather than raising a second,
  redundant error for the same underlying mistake.

### G. Bare basename references resolve

Checks C, D, and F all require a literal `docs/` prefix before the filename they validate. A
reference written as a bare, backtick-wrapped basename in prose — `` `<name>.md` §<N> ``, with no
`docs/` prefix and no markdown link target at all — matches none of them, and was invisible to
every check before this one.

**Why.** Two live examples were found surviving a clean run: one doc pointed at a `§12` in
`theming.md` that had been renumbered away entirely, and another named a `layout.md` that no longer
exists, the material it wanted having moved into
[`layout/module-card.md`](../layout/module-card.md). Both became real markdown links, so check B
guards them going forward.

For every in-scope file, a backtick-wrapped `` `<name>.md` `` token — outside markdown link syntax
(the whole `[text](target)` span is masked out of the line first, the same construct check B
parses) and with no `docs/` or other directory prefix at all — must resolve to **exactly one** real
`docs/**/*.md` file by basename: this is checked every time the name appears, marker or none,
because a basename matching *more than one* doc is unresolvable for a reader regardless of whether
this particular occurrence happens to carry a marker. Separately, whatever immediately follows the
name — whitespace only, up to three characters — is checked as an *optional* trailing
`§N`/`§N.M`/`§N.M.K` marker or `#anchor`: present, and the marker must name a section or anchor
that actually exists in the doc the basename resolved to (or, if the basename resolved to *zero*
docs, that is itself the failure — a doc named that no longer exists anywhere); absent, there is
nothing further to check — a bare name with no marker is casual prose, not a structured
cross-reference, and measured against the real tree, every genuine cross-reference this check is
meant to gate follows the "basename plus marker" shape. Either way, the fix is the same: write the
full `docs/` path (turning it into something checks B/C/D/F already cover). **Not baselined — zero
tolerance**, same as B/C/D/E/F.

Check G does not reimplement anything check B, C, or D already got right:

- **Slug table** — a trailing `#anchor` is checked against the exact same slug table check B/F
  build, never a second implementation.
- **Section table** — a trailing `§N` is checked against `build_headings_file`'s table, the same
  one check D itself reads — check D's own heading-number table was pulled out into this shared
  builder specifically so check G could reuse it instead of adding a third section-resolution
  implementation.
- **No double-reporting** — a bare name that *is* written as proper markdown link syntax (as
  `` [`name.md`](target) ``, link text and target both) is check B's business, not check G's; the
  whole link span is masked out of the line before check G ever looks at it.

## Naming ratchet

Check A works exactly like the [file-size](file-size-guard.md#strict-ratchet-baseline) and
[function-size](function-size-guard.md#strict-ratchet-baseline) guards' baselines, with one entry kind
(`naming <path>`) instead of several, in `scripts/docs-baseline.txt`:

- A baselined file is presence-only grandfathered — fixing it (renaming to kebab-case) makes the
  entry stale, and `--update` removes it.
- No file may join the baseline as new. A newly authored doc that violates the convention fails
  outright, naming the rename it needs.
- `--update` never adds or raises an entry without `--allow-growth`, mirroring the file-size
  guard's rule — it refuses, baseline left untouched, rather than launder a newly-violating
  file through silently. `--allow-growth` is the deliberate, reviewed exception, printing a
  `::warning::` per new entry so it stays visible in CI logs and PR review.
- A first-ever `--update` (no baseline file present yet) bootstraps without needing
  `--allow-growth` — there's nothing to compare against yet, so nothing can be a growth.

Checks B, C, D, E, F, and G have no baseline at all — they're always a hard failure. A broken link,
a stale `docs/...` mention, a stale `§`-section reference, a `docs/README.md` map gap, a stale
`#anchor` mention, or an unresolvable bare basename reference is never something to grandfather;
each is wrong the moment it exists; the ratchet exists only to migrate the legacy filename
convention without a disruptive mass rename.

### Ordering: `git mv`, then `--update` — never a plain check in between

`scripts/docs-baseline.txt` lists doc paths by construction (every `naming <path>` line names a
docs/**/*.md file, and the file's own mechanism comment above names several more). Now that `.txt`
is in `EXTENSIONS` (see the widened-scope note above), the baseline is itself scanned by check C
like any other in-scope file. That creates a real ordering trap for a PR that renames docs
wholesale: right after the `git mv` but before `check-docs.sh --update` regenerates the baseline,
the baseline still names the pre-mv path — which check C now
correctly flags as a `docs/...` mention that doesn't resolve, on top of check A's own "stale
baseline entry" error for the same rename. **This is expected and harmless**, verified against a
full scratch copy of this repo: `--update` never reads the *old* baseline content to
decide anything — it recomputes every naming violation from the tree as it stands right now and
overwrites the file outright — so running `--update` immediately after the `git mv`, in that exact
"intermediate" state, still exits 0 and rewrites a fully clean baseline; a plain check afterward
also passes clean. The trap only bites if a plain (non-`--update`) check is run in the gap between
the `git mv` and the `--update` — which the mandated sequence (`git mv` → `check-docs.sh --update` →
commit both together) never does. `scripts/docs-baseline.txt` therefore stays in scope
for check C rather than being excluded from it: excluding it would have hidden a real class of bug
(a baseline entry left stale — pointing at a path nothing renamed it *to* — after a rename that
missed updating it), for a transient state that never reaches a commit.

## Running it

```bash
bash scripts/check-docs.sh              # exit 1 on any violation
bash scripts/check-docs.sh --list       # every current violation in every check, not just un-baselined ones
bash scripts/check-docs.sh --update     # rewrite the naming baseline from the current tree's check-A violations
bash scripts/check-docs.sh --update --allow-growth   # ...and let a new naming entry through
bash scripts/check-docs.sh --root <dir> # scan a different repo root (used by scripts/tests/check-docs.test.sh)
```

`--list` is the tool for "what's currently wrong, across every check" — useful before starting a
docs cleanup pass, since it doesn't stop at the first failure the way the plain check does.

## Where it runs

- **Pre-commit hook** (`scripts/pre-commit-lint.sh`, installed via `bash scripts/install-hooks.sh`
  — see [Git hooks](local-ci.md#git-hooks)): whole tree, unconditionally, for any commit with
  staged changes — a doc or script edit can introduce a stale reference or map gap just as easily
  as a `Source/**` change can leave a `§`-reference dangling without touching `docs/` at all.
- **`.github/workflows/ci.yml`'s Lint job**, "Check docs integrity" step, next to "Check file
  sizes" — runs unconditionally (no `paths:` filter of its own; the Lint job always runs), so a PR
  that only touches a `Source/*.h` comment (in scope for check C/D, but never under `docs/**`) is
  still covered even though `ci.yml`'s own trigger excludes `docs/**` and `*.md`.
  "Test docs checker" (`scripts/tests/check-docs.test.sh`) runs alongside it, checking the checker
  itself against fixtures — a hole in the scanner (a mis-scanned exclusion, a regex matching the
  wrong `]`, a ratchet that doesn't actually ratchet) is exactly the kind of bug fixture tests
  catch that a real-tree run — which only ever sees today's violations — cannot.
- **`.github/workflows/docs.yml`'s Docs job**: covers the other half of the coverage gap —
  `ci.yml`'s Lint job only runs on `paths:` that touch code/scripts/CI config, so a docs-only PR
  (a rename, a rewritten section, a link fixup) that changes nothing else can skip the Lint job
  entirely. This workflow has no `paths:` filter of its own, so it fires on every pull request,
  docs-only or not; at a few seconds and compiler-free there's no cost to always running it, and a
  path-filtered "docs checker" workflow would just relocate the coverage hole rather than close it.

Between the three, every commit and every PR gets checked at least once before merge, and a commit
that would fail is caught locally before it's ever pushed.

## Enforcement

Running the check is not the same as it gating a merge. The "Docs" job is a required status check
in `main`'s branch protection, and `.github/workflows/ci-passthrough.yml` covers `ci.yml`'s four
path-filtered jobs on a docs-only PR — see
[`ci-pipeline.md`](ci-pipeline.md#docs-only-pull-requests) for both mechanisms and the required
check names.

**Why both are needed.** While the Docs job ran on every PR but was not required, a red Docs job
blocked nothing — and a docs-only PR was blocked anyway, because `ci.yml`'s four required jobs never
post a status when its `paths:` filter excludes `docs/**` and `*.md`. The routine workaround, `gh pr
merge --admin`, bypassed every check including a red Docs job. Making "Docs" required without the
passthrough would have left docs-only PRs unmergeable; adding the passthrough without making "Docs"
required would have left the guard advisory.

## Zero tolerance for stale references

Checks B, C, D, E, F, and G exist specifically because [check A's grandfathering](#naming-ratchet)
doesn't generalize: a stale link, section reference, anchor mention, or unresolvable bare basename
is never "legacy debt to migrate later" the way an old filename is — it actively misdirects the
next reader the moment it goes stale. That's why only the naming convention gets a ratchet at all,
and why fixing a check-B/C/D/E/F/G violation means finding the CORRECT destination (via git history
for a moved/renumbered/renamed section, never a guess) and pointing at that, not adding an
exception anywhere.
