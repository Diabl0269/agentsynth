# The Assets binary-data library (fonts, icons, MIDI Remote controller templates), split out of
# the root CMakeLists.txt purely to keep that file under the repo's 1,000-line file-size cap
# (scripts/check-file-sizes.sh). Paths are ${CMAKE_SOURCE_DIR}-absolute because juceaide runs from
# the calling list file's directory (cmake/), where a repo-relative path would not resolve. A new
# asset (e.g. a controller template, docs/control/midi-remote-ui.md#contribute-a-template) goes here.
juce_add_binary_data(Assets SOURCES
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/JetBrainsMono-Medium.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Manrope-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Manrope-Medium.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Manrope-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/Manrope-Bold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/SpaceMono-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/SpaceMono-Bold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexSans-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexSans-Medium.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexSans-SemiBold.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf
    # Icons (23 SVGs, hyphen-named; BinaryData symbols: e.g. transport_play_svg)
    ${CMAKE_SOURCE_DIR}/assets/icons/transport-play.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/transport-stop.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-undo.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-redo.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-new.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-save.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-load.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-settings.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-auto-arrange.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/action-feedback.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/toggle-ai.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/toggle-matrix.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/toggle-library.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/theme-toggle.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/module-bypass.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/module-mute.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/module-delete.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-sources.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-sequencing.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-envelopes.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-filters.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-modulation-fx.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-time-fx.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-dynamics.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-utility.svg
    # Waveform glyphs (Phase 4 — 4 SVGs, BinaryData symbols: waveformsine_svg etc.)
    ${CMAKE_SOURCE_DIR}/assets/icons/waveform-sine.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/waveform-saw.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/waveform-square.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/waveform-triangle.svg
    # Minimap toggle (issue #159).
    ${CMAKE_SOURCE_DIR}/assets/icons/toggle-minimap.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/module-dual-io.svg
    # Timeline edit-tool strip (Cubase-style tools; see EditTool.h) — 6 SVGs, BinaryData
    # symbols: toolselect_svg, toolsplit_svg, toolglue_svg, toolerase_svg, toolmute_svg,
    # tooldraw_svg.
    ${CMAKE_SOURCE_DIR}/assets/icons/tool-select.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/tool-split.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/tool-glue.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/tool-erase.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/tool-mute.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/tool-draw.svg
    # Track-header kind glyphs + follow-playhead toggle — 4 SVGs, BinaryData symbols:
    # trackmidi_svg, trackaudio_svg, trackautomation_svg, followplayhead_svg.
    ${CMAKE_SOURCE_DIR}/assets/icons/track-midi.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/track-audio.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/track-automation.svg
    ${CMAKE_SOURCE_DIR}/assets/icons/follow-playhead.svg
    # I/O category icon (speaker glyph) — Audio Input/Output library rows and the Audio Output
    # card's identity treatment; both previously fell back to CatUtility.
    ${CMAKE_SOURCE_DIR}/assets/icons/cat-io.svg
    # FRO12 (P9-6, docs/mixer/panel.md): detach-to-window icon, shared by DetachablePanelHost for
    # both the Timeline and Mixer panels. BinaryData symbol: actiondetachwindow_svg.
    ${CMAKE_SOURCE_DIR}/assets/icons/action-detach-window.svg
    # FRO134/FRO143 (docs/control/midi-remote-ui.md#templates-and-importexport): controller
    # templates (generic + vendor), enumerated via BinaryData::namedResourceList by id, not name.
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-8-knobs.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-8-faders-8-buttons.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-transport-strip.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-keyboard-8-knobs.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-korg-nanokontrol2.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-arturia-minilab-3.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-novation-launch-control-xl-3.json
    ${CMAKE_SOURCE_DIR}/assets/midi-remote-templates/template-arturia-beatstep.json
)
