# Function-Size Cap

`scripts/check-function-sizes.sh` enforces a hard **200-line cap** (`FUNCTION_SIZE_CAP`) per
function over every git-tracked `*.cpp`/`*.h`/`*.mm` file under `Source/`, `Tests/` and `Tools/` —
a narrower scope than [`file-size-guard.md`](file-size-guard.md), which scans the whole tree, since
a function only exists in code. It runs in [the Lint job](ci-pipeline.md#jobs)'s "Check function
sizes" step, directly from [`local-ci.md`](local-ci.md)'s local reproduction, and in the pre-commit
hook. `scripts/tests/check-function-sizes.test.sh` covers the checker against fixtures plus one
real-tree case.

**Why.** The root `CLAUDE.md` rule "a new function must never be 200+ lines" had nothing enforcing
it. A function is where a file-size violation actually starts — one function that grew, not evenly
distributed bulk — and it is the more direct signal that a piece of code is doing more than one
thing.

## Tool choice: an awk brace-depth scanner, not `clang-tidy`

`readability-function-size` needs a `compile_commands.json`, meaning the full JUCE `FetchContent`,
a configure, and parsing every JUCE-heavy translation unit — in the one job meant to give fast
feedback *before* a real build. The Lint job never configures CMake. A scanner in the same style as
the file-size guard (no compiler, no network) runs identically in the Lint job, `ci-local.sh` and
the pre-commit hook.

The scanner itself lives in `scripts/function-size-scan.awk`, a separate file from
`check-function-sizes.sh` (unlike the file-size guard's inline awk block), because real brace and
paren-depth parsing needs several helper functions, and its operator-name handling needs literal
`'` characters that would terminate a bash single-quoted script.

## How a function's size is measured

Brace depth, tracked per file. On every `{` not already inside a function, the text since the
previous `;`/`{`/`}` at that level — the "header" — is classified:

- `namespace …` / `extern "C"` and `class`/`struct`/`union`/`enum` (when not itself a function-like
  macro call) are **transparent**: a member function or a nested type inside either is still
  classified normally.
- A `TEST`/`TEST_F`/`TEST_P` macro body is a function named after the whole macro call
  (`TEST_F(Suite, Case)`).
- A header with a top-level `(...)` whose close is followed only by whitespace, `const`,
  `noexcept[(...)]`, `override`, `final`, a trailing `-> type`, or a constructor initializer list
  (`: ...`) is a real function.
- Anything else — a brace initializer, a bare block — is **not** tracked.

Once inside a function, every nested `{` (an `if`/`for`/`while` body, a lambda, a local block)
counts toward that ONE enclosing function and is never reclassified: lambdas and local blocks count
toward the enclosing function. A function's size is `<last line (the closing brace)> - <first line
of its signature> + 1`; a multi-line signature's start line is the signature's own first line, not
the line the `{` lands on. The function's **name** is the qualified identifier — or operator name
(`operator==`, `operator()`, `MainComponent::operator[]`) — immediately before its argument list; a
name repeated within one file gets `#2`, `#3` suffixes in the order it appears.

Comments, string/char literals and preprocessor lines never contribute real code structure:
`//` and `/* */` comments and preprocessor directives are dropped entirely before scanning, and a
string/char literal keeps its text (needed so `extern "C"` is still recognized) but has `{`, `}`,
`(`, `)`, `;` inside it blanked — several `Tests/*.cpp` files embed multi-line `R"(...)"` JSON
fixtures full of braces, and the bare-delimiter raw-string form is tracked across lines the same
way.

One access-specifier quirk worth knowing: `public:`/`private:`/`protected:` on their own line reset
the header buffer exactly like a `;` would, or the NEXT member's header — and its reported start
line — would glue onto the specifier's own line. Every other use of `:` (inheritance, a constructor
initializer list, a ternary) is left alone, since only a header buffer that is EXACTLY one of those
three keywords triggers it.

See `function-size-scan.awk`'s own header comment for the full method and its "KNOWN LIMITATIONS"
section: an explicit-delimiter raw string (`R"DELIM(...)DELIM"`) is not specially handled, since the
codebase only uses the bare-delimiter form, and code inside a disabled `#if 0`/`#ifdef` block is
still scanned as if compiled, since only the preprocessor LINES themselves are dropped, not what
they exclude.

## Strict ratchet baseline

`scripts/function-size-baseline.txt` (`<lines> <path>::<name>` per entry) works exactly like the
file-size baseline: it grandfathers functions already over the cap at their EXACT current size and
only ever tightens. Grow past the entry and the check fails, naming the function, its `path:line`
and its size; shrink it and `--update` must tighten the entry; get it under the cap and `--update`
removes the entry; no function may join the baseline as new — `--update` refuses growth and
additions without `--allow-growth`, which prints a `::warning::` per exception. A same-name,
same-size function whose FILE git confirms was renamed is accepted by `--update` without
`--allow-growth`, the same as a file-size baseline entry surviving a `git mv`.

`bash scripts/check-function-sizes.sh --list [N]` prints the N largest scanned functions (default
25), largest first, regardless of cap or baseline. The baseline file itself is the current list of
everything over the cap.

How to split an over-cap function is the root `CLAUDE.md`'s "Code structure" rule: extract a named
step, or a real collaborator class when the concern has its own state.
