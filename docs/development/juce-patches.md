# Patches to JUCE

JUCE is fetched by `FetchContent` at a pinned tag (`cmake/DependencyVersions.cmake`) and not vendored, so
a change to JUCE itself is a patch file applied after the fetch. There is one:

| Patch | What it adds | Why |
|---|---|---|
| `cmake/patches/juce-vst3-context-menu.patch` | `juce::VST3PluginFormat::setHostContextMenuExtension(AudioPluginInstance&, std::function<void(uint32 paramId, PopupMenu&)>)` | A VST3 plugin asks the host for a parameter's context menu with `IComponentHandler3::createContextMenu(view, &paramId)`. Stock JUCE drops the `ParamID` and returns only the plugin's own items. With the hook the app appends "Add to card" ([`plugin-card-layout.md`](../control/plugin-card-layout.md#add-to-card-from-the-plugin-window)). |

The patch changes `juce_VST3PluginFormat.cpp`/`.h` only, does nothing for an instance without a hook, and
never touches AU or any other format. Items the callback adds are re-added below the plugin's own items
(after a separator) with a reserved id, carry their own `PopupMenu::Item::action`, and JUCE runs that action
on the message thread after the menu closes, also in the async path used because the app builds with
`JUCE_MODAL_LOOPS_PERMITTED` off. A plugin's own `executeMenuItem` is never called for them.

## How it is applied

`CMakeLists.txt` passes `PATCH_COMMAND` to `FetchContent_Declare(juce ...)`, which runs
`cmake/ApplyJucePatch.cmake` in the JUCE source root. FetchContent re-runs its patch step on later configures
of the same tree, so the script is idempotent: a patch that applies forward is applied, one that applies in
reverse is left alone, anything else is a hard error naming the patch. `git apply --ignore-whitespace` is used
because JUCE's files are CRLF and a checkout may rewrite line endings; `.gitattributes` marks
`cmake/patches/*.patch` as `-text` so this repo does not rewrite the patch itself. Git is on every runner.
`scripts/tests/juce-patch.test.sh` tests the script against a fixture tree and the real patch against a JUCE
checkout when one is on disk.

Two things bypass the patch step and must be kept in mind:

- **CI caches.** The cached `build/_deps` holds the patched tree, so its key hashes the patch files and the
  apply script ([`ci-caching.md`](ci-caching.md)); the generation prefix was bumped when the patch was added.
- **`FETCHCONTENT_SOURCE_DIR_JUCE`.** A source dir given on the command line skips the patch step.
  `scripts/ci-local.sh`'s worktree reuse therefore reuses the main checkout's JUCE only when it already carries
  the worktree's patches ([`local-ci.md`](local-ci.md)).

## Refreshing a patch (new JUCE tag, or a change to the hook)

1. Copy the two files the patch touches from an unpatched checkout of the pinned tag into a scratch git repo
   and commit them. Keep their CRLF endings (edit with a tool that preserves them).
2. Make the change, then `git diff` there; put the explanatory header block (text before the first
   `diff --git` is ignored by `git apply`) back on top and save as `cmake/patches/<name>.patch`.
3. `bash scripts/tests/juce-patch.test.sh`, then a fresh `cmake -S . -B <new build dir>` so FetchContent applies
   it itself.
