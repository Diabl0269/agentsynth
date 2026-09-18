# ASCII-Only String Literals

`scripts/tests/check-nonascii-literals.test.sh` (no compiler, about a second) fails
[the Lint job](ci-pipeline.md#jobs) when a `Source/**.{cpp,h}` line puts a non-ASCII byte inside a
double-quoted literal. Write plain ASCII, or declare the encoding with `juce::CharPointer_UTF8` /
`juce::String::fromUTF8` — a line mentioning either is exempt.

**Why.** `juce::String`'s `const char*` constructor decodes its bytes with `CharPointer_ASCII` —
**Latin-1, not UTF-8** — so `"Rename…"` reaches the UI as `"Renameâ€¦"`, one mojibake glyph per
byte. Nothing else in the toolchain enforces this. A hex escape (`"Rename\xe2\x80\xa6"`) is the
*identical* three bytes and fails the same way; that spelling is how the bug shipped a second time
after the first fix, which is why the checker flags `\x`/`\u` escapes above `0x7F` as well as raw
bytes.

## Scope and limits

All deliberate:

- **Comments are exempt.** This codebase writes prose em dashes throughout them and they never reach
  `juce::String`.
- **`Tests/` is out of scope.** Its non-ASCII lives in gtest `<<` streams, which go to a
  `std::ostream` and render fine.
- **A line opening a raw string literal (`R"(...)"`) is skipped** rather than parsed, since it has
  its own quoting rules.
- The scanner (`scripts/nonascii-literals.py`) is a byte-level state machine that tracks
  string/char/line-comment/block-comment state, **not a C++ parser**. It does not model octal
  escapes or line continuations inside a literal.

A sibling check, `scripts/utf8-literal-check.sh`, runs directly in the Lint job and from
[`local-ci.md`](local-ci.md)'s local reproduction, covering un-decoded UTF-8 escapes on the same
reasoning.
