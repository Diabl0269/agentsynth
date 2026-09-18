# Header Comment Placement

`scripts/check-header-comments.sh` enforces comment **placement**, not volume, on every git-tracked
`Source/**/*.h` / `Source/**/*.hpp` header. It runs in [the Lint job](ci-pipeline.md#jobs)'s "Check
header comment placement" step, directly from [`local-ci.md`](local-ci.md)'s local reproduction, and
in the pre-commit hook. `scripts/tests/check-header-comments.test.sh` covers the checker itself
against fixtures.

The rule it checks is stated in the root `CLAUDE.md`'s "Code structure" section: a shared class
header carries declarations, the class-level comment, the section banners, and — per member — only
the constraints a *caller* can violate from outside (nullability, thread affinity, call-ordering
preconditions, units, ownership), one line each. The *maintainer*-facing rationale — why it is
implemented this way, the threading or ordering argument, the edge case that forced the design, the
invariants the body must preserve — belongs as a doc comment beside the out-of-line definition in
the owning `.cpp` unit instead.

**Why.** Only a comment sitting inside the diff hunk of the change that invalidates it reliably
stays true, and a header comment recompiles every including translation unit on every edit. Several
headers in this repo had drifted badly from that — more comment lines than code — because nothing
enforced moving detail out as a header grew.

**Why placement, not a volume cap.** A header can legitimately be *comment-heavy*: a small,
genuinely subtle type's invariant can outweigh its declaration, and that is fine. What is not fine
is a maintainer-facing contract sitting in the header when it has a `.cpp` unit right there to live
beside instead. So the threshold looks for comments crowding out code specifically where there is
somewhere better for them to go.

## Tracked quantity: excess, not a raw comment count

The guard ratchets `EXCESS = (comment lines) - (code lines)`, a single integer that may be negative
— deliberately not the comment-line count on its own.

**Why.** The rule is satisfied just as well by the ordinary, mandated way of adding a class member
as it is by leaving a compliant header alone: declare the member and give it its own short one-line
caller-facing contract right there in the header (`+1` comment line, `+1` code line) — excess is
unchanged. A raw comment-count ratchet would reject that exact edit outright the moment a baselined
header picked up a new member, training everyone to reach for `--allow-growth` reflexively on every
baselined header instead of only when someone genuinely piles more prose in without matching code.
Tracking excess means only the real failure mode — comment growing faster than code — moves the
needle.

## The exact threshold

A header is flagged when ALL of the following hold:

1. Excess (comment lines minus code lines) is positive.
2. Comment lines are at or above `HEADER_COMMENT_FLOOR` (default 60) — the floor that exempts a
   small type whose invariant is genuinely longer than its declaration.
3. At least one tracked `<dir>/<base>*.cpp` sibling exists for header `<dir>/<base>.h(pp)` —
   somewhere to move the rationale *to*.

Condition 3 is a **prefix match scoped to the header's own directory**. It is not a stem-exact
`<dir>/<base>.cpp`-only test: a class split into per-concern units (`Source/AudioEngine/AudioEngine.h`)
has no `AudioEngine.cpp` of its own but eight `AudioEngine*.cpp` units, and must still be flagged.
It is not "any `.cpp` in the directory" either: that broader test would wrongly un-exempt a
genuinely header-only class sharing a directory with unrelated classes' `.cpp` files. A header-only
component with no owning `.cpp` at all (`Source/Timeline/EpochExchange.h`,
`Source/UI/Graph/GraphCanvasHost.h`, `Source/UI/Layout/UIAnimation.h`,
`Source/Plugin/Hosting/HostedPluginWindowManager.h`) is exempt: its rationale has no out-of-line
home, and this repo deliberately keeps that content where it is.

**Counting is a heuristic, not a C++ parser** — no attempt is made to recognize `//` or `/*` inside
a string or char literal. Per line, after stripping leading whitespace: blank counts as neither; a
line starting `//` is a comment line; a line starting `/*` opens a block comment (or closes
immediately if the same line also contains `*/`), and every line up to and including the closing
`*/` counts as a comment line; everything else is a code line — deliberately including a code line
with a trailing `// note`, since a trailing comment already sits beside the thing it explains and is
not a placement problem.

## How to fix a violation

**Relocate — never delete.** The content is genuinely valuable why/invariant material; it just
belongs beside the out-of-line definition it describes rather than in the header. Move each flagged
member's detailed contract into a doc comment directly above its definition in the owning `.cpp`
unit, leaving only the short caller-facing constraint (if any) behind in the header. Adding a
brand-new member with its own one-line caller contract is not a violation and never trips the
ratchet, per the excess-tracking reasoning above.

## Strict ratchet baseline

`scripts/header-comment-baseline.txt` (`<excess> <path>` per entry — the same one-integer-plus-path
shape as `scripts/file-size-baseline.txt`) works exactly like the
[file-size](file-size-guard.md#strict-ratchet-baseline) and
[function-size](function-size-guard.md#strict-ratchet-baseline) baselines. It grandfathers headers
already over the threshold at their EXACT current excess, and only ever tightens:

- A baselined header's excess may never grow past its entry — do that and the check fails, naming
  the exact growth.
- Shrink one and its entry must tighten to match: run `bash scripts/check-header-comments.sh
  --update`.
- Get a header's excess back to zero or below and its entry must be removed entirely — `--update`
  does that too.
- No header may join the baseline as new; a header crossing the threshold for the first time fails
  outright.
- `--update` never raises an entry or adds a new one either — it refuses (exit 1, baseline left
  untouched) unless `--allow-growth`, the deliberate, reviewed exception, which prints a
  `::warning::` per raised or added entry.

Unlike the file-size guard, this one does **not** special-case a git-confirmed rename: a header
split or renamed almost never keeps an identical excess, since the whole point of a split is to move
comments out, so that machinery had no real payoff here.

`bash scripts/check-header-comments.sh --list [N]` prints the N most comment-heavy scanned headers
(default 25), as `<comments> <code> <excess> <path>` sorted by excess descending, regardless of
threshold or baseline — for picking what to relocate next. The baseline file itself is the current
list of everything over the threshold.

## Scope

Git-tracked `Source/**/*.h` and `Source/**/*.hpp` only. `Tests/` is deliberately out of scope: that
tree already runs a healthy comment-to-code ratio, and its fixtures and mocks are not the
maintainer-facing contracts this guard exists to relocate. The guard's own baseline file is excluded
from itself.
