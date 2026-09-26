# The Assets binary-data library (fonts, icons, MIDI Remote controller templates), split out of
# the root CMakeLists.txt purely to keep that file under the repo's 1,000-line file-size cap
# (scripts/check-file-sizes.sh). No behavior change: paths stay relative to the repo root. A new
# asset (e.g. a controller template, docs/control/midi-remote-ui.md#contribute-a-template) goes here.
juce_add_binary_data(Assets SOURCES
    assets/fonts/Inter-Regular.ttf
    assets/fonts/Inter-Medium.ttf
    assets/fonts/Inter-SemiBold.ttf
    assets/fonts/Inter-Bold.ttf
    assets/fonts/JetBrainsMono-Regular.ttf
    assets/fonts/JetBrainsMono-Medium.ttf
    assets/fonts/Manrope-Regular.ttf
    assets/fonts/Manrope-Medium.ttf
    assets/fonts/Manrope-SemiBold.ttf
    assets/fonts/Manrope-Bold.ttf
    assets/fonts/SpaceMono-Regular.ttf
    assets/fonts/SpaceMono-Bold.ttf
    assets/fonts/IBMPlexSans-Regular.ttf
    assets/fonts/IBMPlexSans-Medium.ttf
    assets/fonts/IBMPlexSans-SemiBold.ttf
    assets/fonts/IBMPlexMono-Regular.ttf
    assets/fonts/IBMPlexMono-Medium.ttf
    # Icons (23 SVGs, hyphen-named; BinaryData symbols: e.g. transport_play_svg)
    assets/icons/transport-play.svg
    assets/icons/transport-stop.svg
    assets/icons/action-undo.svg
    assets/icons/action-redo.svg
    assets/icons/action-new.svg
    assets/icons/action-save.svg
    assets/icons/action-load.svg
    assets/icons/action-settings.svg
    assets/icons/action-auto-arrange.svg
    assets/icons/action-feedback.svg
    assets/icons/toggle-ai.svg
    assets/icons/toggle-matrix.svg
    assets/icons/toggle-library.svg
    assets/icons/theme-toggle.svg
    assets/icons/module-bypass.svg
    assets/icons/module-mute.svg
    assets/icons/module-delete.svg
    assets/icons/cat-sources.svg
    assets/icons/cat-sequencing.svg
    assets/icons/cat-envelopes.svg
    assets/icons/cat-filters.svg
    assets/icons/cat-modulation-fx.svg
    assets/icons/cat-time-fx.svg
    assets/icons/cat-dynamics.svg
    assets/icons/cat-utility.svg
    # Waveform glyphs (Phase 4 — 4 SVGs, BinaryData symbols: waveformsine_svg etc.)
    assets/icons/waveform-sine.svg
    assets/icons/waveform-saw.svg
    assets/icons/waveform-square.svg
    assets/icons/waveform-triangle.svg
    # Minimap toggle (issue #159).
    assets/icons/toggle-minimap.svg
    assets/icons/module-dual-io.svg
    # Timeline edit-tool strip (Cubase-style tools; see EditTool.h) — 6 SVGs, BinaryData
    # symbols: toolselect_svg, toolsplit_svg, toolglue_svg, toolerase_svg, toolmute_svg,
    # tooldraw_svg.
    assets/icons/tool-select.svg
    assets/icons/tool-split.svg
    assets/icons/tool-glue.svg
    assets/icons/tool-erase.svg
    assets/icons/tool-mute.svg
    assets/icons/tool-draw.svg
    # Track-header kind glyphs + follow-playhead toggle — 4 SVGs, BinaryData symbols:
    # trackmidi_svg, trackaudio_svg, trackautomation_svg, followplayhead_svg.
    assets/icons/track-midi.svg
    assets/icons/track-audio.svg
    assets/icons/track-automation.svg
    assets/icons/follow-playhead.svg
    # I/O category icon (speaker glyph) — Audio Input/Output library rows and the Audio Output
    # card's identity treatment; both previously fell back to CatUtility.
    assets/icons/cat-io.svg
    # FRO12 (P9-6, docs/mixer/panel.md): detach-to-window icon, shared by DetachablePanelHost for
    # both the Timeline and Mixer panels. BinaryData symbol: actiondetachwindow_svg.
    assets/icons/action-detach-window.svg
    # FRO134/FRO143 (docs/control/midi-remote-ui.md#templates-and-importexport): controller
    # templates (generic + vendor), enumerated via BinaryData::namedResourceList by id, not name.
    assets/midi-remote-templates/template-8-knobs.json
    assets/midi-remote-templates/template-8-faders-8-buttons.json
    assets/midi-remote-templates/template-transport-strip.json
    assets/midi-remote-templates/template-keyboard-8-knobs.json
    assets/midi-remote-templates/template-korg-nanokontrol2.json
    assets/midi-remote-templates/template-arturia-minilab-3.json
)
