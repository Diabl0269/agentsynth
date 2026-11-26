# What a request shows the model: collapsed tracks

A request used to carry the whole patch. On a big project most of it is channel plumbing (Track In, EQ, Compressor, Channel Strip, attenuverters), so the request is large and a small local model loses precision. With a scope context installed, each **track** (a channel macro, `synth::isChannelMacro`) is sent as one line instead of its modules and cables.

## What collapses

`AIIntegrationService::setScopeContext(macros, selectedNodes)` (installed by `MainComponentSetup.cpp`) turns it on. Without it (tests, the plugin, a standalone service) the whole patch is sent as before.

A track stays **expanded** when:

- the request names it: its macro name appears as a whole word, ignoring case ("make the bass wobble" names `Bass`; "bassoon" does not);
- a selected node is a member of it or of a macro inside it;
- it is an ancestor or descendant of an expanded macro.

Plain groups and loose modules always stay as they are.

A collapsed track's modules, and every cable or modulation that touches one of them (crossing cables included), are removed from the patch JSON. One line per track follows the JSON in the request text: `- "Bass": 12 modules, ports: in "Audio In" (stereo), out "Out" (stereo)`, with a note that the track cannot be edited from this view and the user must name it in the request. A last line lists the node ids the collapsed tracks use, as ranges (`Node ids used by the collapsed tracks (a new module must not reuse one): 16-22, 24-91`), so a new module the model adds never takes an id a hidden node already has (BAC163); no line when nothing is hidden.

The lines ride in the request text, not in `currentPatch`: the hosted `currentPatch` is a typed patch the server parses, which would drop an unknown key. The hosted request puts them after the user's prompt (`userPrompt`); the local chat puts them after the patch section.

## Replace becomes merge

A replace answered from a collapsed picture would delete every hidden module. So while the last request built left a track out (`lastContextWasCollapsed()`), a patch or plan that states `"replace"` is applied as a **merge**: `applyPatch` (which reports it as `didLastPatchRepairMode()`), the edit plan's mode, and the previews all agree. The prompt also asks for `"mode": "merge"`. `validatePatch` is unchanged.

## Measured

Tal's "My own creation" (76 modules, 8 tracks) was 24,392 characters of patch JSON; the design is in [`ai-chat-edit-plan`](../../../docs/ai-chat-edit-plan.md#large-graphs-scope-the-context-do-not-change-the-loop).
