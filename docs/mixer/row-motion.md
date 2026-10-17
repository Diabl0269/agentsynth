# Insert and send row motion

An insert or send row that is added or removed opens or closes where it was. The lists are rebuilt as new objects on
every edit, so `MixerPanelComponent::takeOrBuildStripColumn` takes a `MixerRowsMotion::Snapshot` of the old column
(`captureRowSnapshot`: the rows' keys and, for a removal, a picture of the section) and `rebuildFrom` hands it to the
new column (`playRowMotion`) once it is laid out. Master keeps its column across edits and does the same inside
`setColumn`. Rows are keyed by module uuid (a split row by its fork's uuid) and by send slot, so a drag-reorder, a
retarget or a toggle changes no key and does not animate. A change that adds and removes together, a section scrolled
off its top, or an off-screen panel lands at once. The motion is the shared `ExitEnterListMotion`
([animation](../layout/animation.md#delete-and-undo-animation)). Tests: `Tests/UI/Mixer/MixerRowsMotionTests.cpp`.
