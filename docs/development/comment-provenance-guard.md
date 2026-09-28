# Comment Provenance

`scripts/check-comment-provenance.sh` keeps **provenance** out of code comments: ticket ids, dates
and the other markers of *when and why a change was made* rather than *what the code does*. It
scans every git-tracked `Source/**` and `Tests/**` C/C++/Objective-C file (`.h`, `.hpp`, `.cpp`,
`.mm`, `.c`) and runs in [the Lint job](ci-pipeline.md#jobs)'s "Check comment provenance" step,
directly from [`local-ci.md`](local-ci.md)'s local reproduction, and in the pre-commit hook.
`scripts/tests/check-comment-provenance.test.sh` covers the checker itself against fixtures.

**Why.** A comment should tell the next reader something true about the code as it is now:
behaviour, invariants, the hazard that explains an unusual shape. Provenance answers a different
question, and `git log` / `git blame` answer it better — accurately, with the diff, and without
going stale. A comment that says which ticket introduced a line is wrong the day the line is
refactored, and a tree full of them teaches readers to skim comments instead of trusting them.

## What is counted

Per file, the number of **lines** whose comment text matches (extended regex, case-sensitive,
word-bounded):

| Family | Pattern | Example |
|---|---|---|
| Ticket id | `(FRO\|BAC\|WEB\|MAR\|AGE\|PRO)[0-9]+` | `FRO42` |
| Phase/step id | `P[0-9]+-[0-9]+` | `P8-17` |
| Task id | `T1[0-9][0-9]` | `T159` |
| ISO date | `20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]` | `2026-09-28` |

Only **comment text** is matched. A `//` comment runs to the end of its line; a `/* ... */` comment
may span lines, and every line inside it counts as comment text. Code and string or char literals
on the same line are skipped, so a test that builds the string `"2026-09-28"` is not a violation. A
line with several matches counts once.

This is a heuristic scanner, not a C++ parser: it tracks double-quoted strings and char literals
(a quote directly after an alphanumeric is a digit separator such as `1'000`, not a char literal)
but not raw string literals that span lines.

## The rewrite rule

Keep the behavioural statement, delete the provenance:

```cpp
// Before: FRO42 review fix: the Instrument -> Plugin submenu ALWAYS appends the format
// After:  The Instrument -> Plugin submenu always appends the format
```

Also drop the provenance words the pattern cannot catch — "review fix", "round 3 review", "founder
asked", "added in", "as of <date>", "previously X, now Y" chronicles, "was a bug where…"
postmortems. Keep a short statement of the hazard when it explains *why* the code is shaped this
way ("`createBus()` rebuilds the mixer synchronously, so re-fetch the strip afterwards"). A comment
that is pure narrative with no behavioural content is deleted outright. A cross-reference to a
ticket becomes a reference to the doc, class or test that now carries the knowledge
(`see docs/development/testing.md`), or is dropped. Only comments change — never code, string literals
(test-data dates stay), identifiers or test names.

## The exception: `Regression test for <ID>:` (Tests/ only)

A test that genuinely pins one specific past bug may cite it, in exactly this form, on the comment
line directly above its `TEST`/`TEST_F`/`TEST_P` (or the first line of its body):

```cpp
// Regression test for FRO97: <one line: what broke>
```

A line whose comment text matches `Regression test for (FRO|BAC|WEB|MAR|AGE|PRO)[0-9]+:` is not
counted **under `Tests/`**. The same line under `Source/` is counted like any other provenance, the
colon is required, and the whole line is exempt, so keep dates out of it by convention — they never
belong there. Do not invent a citation to silence the guard: only an existing comment that already
names the bug the test pins qualifies. Everything else in `Tests/` is stripped like `Source/`.

## Strict ratchet baseline

`scripts/comment-provenance-baseline.txt` (`<count> <path>` per entry — the same shape as
`scripts/file-size-baseline.txt`) works exactly like the
[file-size](file-size-guard.md#strict-ratchet-baseline) and
[function-size](function-size-guard.md#strict-ratchet-baseline) baselines. It grandfathers files
that still carry provenance at their EXACT current count, and only ever tightens:

- A file not in the baseline must have **zero** matching lines; a new one fails outright, printing
  the file, its count and each offending line.
- A baselined file may never exceed its entry — do that and the check fails, naming the growth.
- Shrink one and its entry must tighten to match: run `bash scripts/check-comment-provenance.sh
  --update`.
- Get a file to zero and its entry must be removed — `--update` does that too.
- `--update` never raises an entry or adds a file — it refuses (exit 1, baseline left untouched)
  unless `--allow-growth`, the deliberate, reviewed exception, which prints a `::warning::` per
  raised or added entry. The first `--update` against a repo with no baseline file yet is a
  bootstrap, not growth.

Like the header-comment guard — and unlike the file-size guard — it does **not** special-case
renames: a renamed file with provenance needs `--allow-growth` once, or better, its comments
rewritten first.

`bash scripts/check-comment-provenance.sh --list [N]` prints the N files with the most matching
lines (default 25) as `<count> <path>`, regardless of the baseline — for picking what to rewrite
next.

## Portability

The scanner is plain POSIX awk (`mawk`, `gawk` and BSD awk all work — word boundaries are spelled
out with character classes rather than `\b`), so the same script runs in the Lint job, in
`ci-local.sh` and in the pre-commit hook. Like its siblings it strips an inherited `GIT_DIR` /
`GIT_WORK_TREE` before asking git anything, and fails loudly rather than passing vacuously if every
scanned file reads back empty.
