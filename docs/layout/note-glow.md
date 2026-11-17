# Note glow: lit keys fade out

A painted keyboard fades its lit keys the same way: `synth::ui::NoteGlow` (`Source/UI/Layout/NoteGlow.h`) keeps one amount
per key on one `FadeAmount`. A key that lights takes the full accent at once (a played note answers the click); a key
that stops sounding fades from its current amount to nothing (110 ms, 80 ms under Reduce Motion, at once under Off), and
a key released or lit again mid-fade starts from where it stands. The Sampler card's key strip and its Edit map's
keyboard both paint through `paintChromaticKeys` with the glow amounts, so they cannot drift.

Tests: `NoteGlow.*` (amounts over a stepped fade) and `SamplerKeyShape.ALitKeyFadesBack...` (the pixels of both keyboards). Scenario: `Tools/ui-verify/scenarios/sampler-key-fade-out.yaml`.
