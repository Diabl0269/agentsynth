# Removing a module's last modulator

A removal that would also delete the LFO (this routing is the last thing it moves) first
shows "Remove LFO 1?": "Cutoff is the last thing LFO 1 moves, so removing it also deletes LFO 1 and its
settings, unless you keep the module. Cmd+Z brings it back." (Ctrl+Z off macOS), a **Remember my choice** box, and
**Remove LFO** (Return) / **Keep module** / **Cancel** (Escape). Cancel changes nothing. Remove LFO removes the
routing and the LFO as one undo step; Keep module removes only the routing (and its amount lane) and leaves the LFO
on the canvas. With the box ticked the answer stands: the preference below turns off and the user setting
`timelineRemoveLastSourceChoice` ("keep" or "remove"; no value reads as "remove") is applied without asking from then
on; a Cancel never stores anything. The question is `confirmRemoveLfo`
(`Modulators/RemoveLfoConfirm.{h,cpp}`, on the shared `showConfirmChoice` window), asked by
`MainComponent::removeModulator` before it calls `performRemoveModulator(modulator, deleteLonelyLfo)`; an LFO with
another destination never asks. Every way of removing a modulation reaches `removeModulator`: the row's menu, the mod
dot panel's remove button, a double-click on the dot and a double-click on the cable (`ModDotController::removeChain`),
so the question and its options are the same on all of them. The preference is the user setting
`timelineAskBeforeRemovingLfo` (default ON), toggled in Settings, Preferences, Timeline ("Ask before removing an
LFO's last destination") and read at use time; turning it back on is the reset: it forgets the remembered answer, so
the next removal asks again.
Test hook: `synth::ui::test_hooks::removeLfoConfirmHookForTest`.
