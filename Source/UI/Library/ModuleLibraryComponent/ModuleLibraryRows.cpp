// ModuleLibraryRows.cpp -- the row/entry model: rebuildEntries() (the module catalogue +
// snippets + plugins -> flat Entry list), buildRows() layout, per-row classification
// (isHeaderEntry/isDraggableEntry/...), row lookup by position, tooltip/description text, and
// the hover/keyboard-focus visibility clamps.
#include "ModuleLibraryComponent.h"

juce::String ModuleLibraryComponent::subsectionKey(const juce::String& section, const juce::String& subHeader) {
    return section + " :: " + subHeader;
}

juce::String ModuleLibraryComponent::descriptionFor(const juce::String& moduleName) {
    if (moduleName.equalsIgnoreCase("Oscillator"))
        return "Generates audio waveforms (sine, saw, square, triangle). Switch Poly on to run "
               "8 voices driven by a Poly MIDI pitch fan.";
    if (moduleName.equalsIgnoreCase("Wavetable"))
        return "Scans through 3D wavetables - six built-ins or load your own file.";
    if (moduleName.equalsIgnoreCase("Noise"))
        return "Generates noise (white, pink, brown).";
    if (moduleName.equalsIgnoreCase("Sampler"))
        return "Plays an audio file back as a sample or scatters it into grains.";
    if (moduleName.equalsIgnoreCase("LFO"))
        return "Low-frequency oscillator for slow cyclic modulation.";
    if (moduleName.equalsIgnoreCase("Sequencer"))
        return "Step sequencer that outputs pitch and gate CV signals.";
    if (moduleName.equalsIgnoreCase("Poly Sequencer"))
        return "Polyphonic step sequencer for multi-voice melodies.";
    if (moduleName.equalsIgnoreCase("MidiKeyboard"))
        return "On-screen MIDI keyboard for note input.";
    if (moduleName.equalsIgnoreCase("Poly MIDI"))
        return "Converts MIDI into 8 voices of pitch and gate CV. Patch Poly Out to an "
               "Oscillator's Pitch and an ADSR's Gate, and switch Poly on for every module in "
               "the chain (Oscillator, ADSR, Filter, VCA) - with Poly off, only one voice sounds.";
    if (moduleName.equalsIgnoreCase("External MIDI"))
        return "Routes external MIDI device input into the patch graph.";
    if (moduleName.equalsIgnoreCase("ADSR"))
        return "Attack-Decay-Sustain-Release envelope generator. Gate CV or MIDI starts the "
               "envelope; Threshold sets how high the gate must rise. Switch Poly on for one "
               "envelope per voice.";
    if (moduleName.equalsIgnoreCase("Envelope Follower"))
        return "Tracks an audio signal's amplitude and outputs it as modulation CV.";
    if (moduleName.equalsIgnoreCase("VCA"))
        return "Voltage-controlled amplifier - controls signal amplitude via CV. Switch Poly on "
               "to gain-control 8 voices and sum them to stereo.";
    if (moduleName.equalsIgnoreCase("Filter"))
        return "Multi-mode resonant filter (low-pass, high-pass, band-pass). Switch Poly on to "
               "filter 8 voices; cutoff and resonance CV stay shared across them.";
    if (moduleName.equalsIgnoreCase("Parametric EQ"))
        return "Four-band EQ with a visual response curve for surgical tone shaping.";
    if (moduleName.equalsIgnoreCase("Chorus"))
        return "Adds lush width by layering slightly detuned copies of the signal.";
    if (moduleName.equalsIgnoreCase("Phaser"))
        return "Sweeping all-pass phase modulation effect.";
    if (moduleName.equalsIgnoreCase("Flanger"))
        return "Short delay feedback comb-filter with a sweeping metallic sound.";
    if (moduleName.equalsIgnoreCase("Distortion"))
        return "Waveshaping distortion from soft saturation to hard clipping.";
    if (moduleName.equalsIgnoreCase("Ring Modulator"))
        return "Oversampled diode-ring modulator - metallic, bell-like sum and difference tones.";
    if (moduleName.equalsIgnoreCase("Bitcrusher"))
        return "For Lo-Fi, sample-rate reduction, and retro digital grit.";
    if (moduleName.equalsIgnoreCase("Pitch Shifter"))
        return "Transposes by semitones or shifts every partial by a fixed number of Hz.";
    if (moduleName.equalsIgnoreCase("Delay"))
        return "Tempo-syncable stereo echo / delay line.";
    if (moduleName.equalsIgnoreCase("Reverb"))
        return "Algorithmic reverb for adding space and depth.";
    if (moduleName.equalsIgnoreCase("Compressor"))
        return "Dynamic range compressor with threshold, ratio, attack and release.";
    if (moduleName.equalsIgnoreCase("Limiter"))
        return "Brickwall limiter that prevents the signal from exceeding 0 dBFS.";
    if (moduleName.equalsIgnoreCase("Gate"))
        return "Noise gate - attenuates the signal below Threshold, with Hold and Range "
               "controlling how it closes.";
    if (moduleName.equalsIgnoreCase("Macros"))
        return "Bank of assignable macro knobs - one knob drives many parameters at once.";
    if (moduleName.equalsIgnoreCase("Sample & Hold"))
        return "Latches a source value on each clock edge for stepped random CV.";
    if (moduleName.equalsIgnoreCase("Voice Mixer"))
        return "Sums multiple polyphonic voices down to a stereo mix.";
    if (moduleName.equalsIgnoreCase("Math"))
        return "Dual-input math/logic utility - Sum, Difference, Min, Max and Product of A and B.";
    if (moduleName.equalsIgnoreCase("Comparator"))
        return "Emits a gate while the Signal is above Threshold, plus the inverted gate. Slice "
               "an LFO, a kick, or any CV into a pulse.";
    if (moduleName.equalsIgnoreCase("Audio Input"))
        return "Audio from the input device - one jack per input channel. Only one per patch.";
    if (moduleName.equalsIgnoreCase("Audio Output"))
        return "Sends the patch to the output device. Only one per patch.";
    // Generic fallback for any unrecognised module name.
    return "Audio processing module.";
}

juce::String ModuleLibraryComponent::snippetDescription(const juce::String& name, int moduleCount) {
    return "Snippet \"" + name + "\" - " + juce::String(moduleCount) + (moduleCount == 1 ? " module. " : " modules. ") +
           "Drag onto the canvas to insert the whole group.";
}

juce::String ModuleLibraryComponent::pluginDescription(const juce::String& name, const juce::String& format) {
    return name + " (" + format +
           ") - a plugin installed on this machine. Drag it onto the canvas, or click "
           "to drop it in the middle.";
}

juce::String ModuleLibraryComponent::scanRowDescription(bool anyPluginsKnown) {
    return anyPluginsKnown ? "Rescan for installed plugins. Each one is checked in its own process, so a plugin "
                             "that crashes cannot take the app down."
                           : "Look for VST3 and Audio Unit plugins installed on this machine. Each one is "
                             "checked in its own process, so a plugin that crashes cannot take the app down.";
}

std::vector<ModuleLibraryComponent::Row> ModuleLibraryComponent::buildRows() const {
    std::vector<Row> rows;
    int y = kPinnedChromeHeight + kFirstRowY;
    bool seenHeader = false;
    const bool filtering = isSearchActive();

    size_t i = 0;
    while (i < entries.size()) {
        if (entries[i].kind != RowKind::Header) {
            ++i; // defensive: today every row follows a header
            continue;
        }

        // Span of rows belonging to this header.
        size_t end = i + 1;
        while (end < entries.size() && entries[end].kind != RowKind::Header)
            ++end;

        std::vector<size_t> visibleChildren;
        if (filtering) {
            if (!sectionVisibleInSearch(i, end)) {
                i = end;
                continue;
            }
            for (size_t j = i + 1; j < end; ++j)
                if (childVisibleInSearch(entries[j]))
                    visibleChildren.push_back(j);
        } else {
            for (size_t j = i + 1; j < end; ++j)
                visibleChildren.push_back(j);
        }

        if (seenHeader)
            y += kHeaderGap;
        seenHeader = true;
        rows.push_back({(int)i, y, kHeaderHeight});
        y += kHeaderHeight;

        // Pass A (subsection-local): fold each plugin-format sub-group independently of the
        // header's own fold, splitting visibleChildren into segments at each SubHeader. A
        // segment with no SubHeader (e.g. the leading "Scan for plugins..." Action row) keeps
        // every row at its natural kItemHeight. When no subsection is ever collapsed this
        // degenerates to one localHeight==kItemHeight entry per visible child, in the same
        // order — which is what keeps Pass B byte-identical to the old single-pass layout.
        struct LocalRow {
            size_t entryIndex;
            int localHeight;
        };
        std::vector<LocalRow> localRows;
        localRows.reserve(visibleChildren.size());
        size_t vc = 0;
        while (vc < visibleChildren.size()) {
            const size_t entryIdx = visibleChildren[vc];
            if (entries[entryIdx].kind != RowKind::SubHeader) {
                localRows.push_back({entryIdx, kItemHeight});
                ++vc;
                continue;
            }

            // The sub-header row itself is never folded by its own progress — only the
            // section header above it can hide it.
            localRows.push_back({entryIdx, kItemHeight});

            const size_t childStart = vc + 1;
            size_t childEnd = childStart;
            while (childEnd < visibleChildren.size() && entries[visibleChildren[childEnd]].kind != RowKind::SubHeader)
                ++childEnd;
            const int childCount = (int)(childEnd - childStart);

            const juce::String subKey = subsectionKey(entries[i].text, entries[entryIdx].text);
            const float subProgress = filtering ? 0.0f : getSectionProgress(subKey);
            const int subNatural = childCount * kItemHeight;
            const int subBand = juce::roundToInt((float)subNatural * (1.0f - subProgress));

            for (int c = 0; c < childCount; ++c) {
                const int localTop = c * kItemHeight;
                const int localHeight = juce::jlimit(0, kItemHeight, subBand - localTop);
                if (localHeight > 0)
                    localRows.push_back({visibleChildren[childStart + (size_t)c], localHeight});
            }

            vc = childEnd;
        }

        // Pass B (header-level): identical shape to the old single-pass algorithm, generalized
        // to sum/advance over Pass A's local heights instead of a flat kItemHeight per row.
        int naturalHeight = 0;
        for (const auto& localRow : localRows)
            naturalHeight += localRow.localHeight;
        // Search forces matching sections open without touching collapse progress, so typing
        // does not fire the accordion (or persist a fold the user never asked for).
        const float progress = filtering ? 0.0f : getSectionProgress(entries[i].text);
        const int bandHeight = juce::roundToInt((float)naturalHeight * (1.0f - progress));
        const int bandTop = y;

        int consumed = 0;
        for (const auto& localRow : localRows) {
            const int rowTop = bandTop + consumed;
            const int visibleHeight = juce::jlimit(0, localRow.localHeight, bandTop + bandHeight - rowTop);
            if (visibleHeight > 0)
                rows.push_back({(int)localRow.entryIndex, rowTop, visibleHeight});
            consumed += localRow.localHeight;
        }

        y = bandTop + bandHeight;
        i = end;
    }
    return rows;
}

int ModuleLibraryComponent::getTotalContentHeight() const {
    auto rows = buildRows();
    return rows.empty() ? kPinnedChromeHeight + kFirstRowY : rows.back().y + rows.back().height + kFirstRowY;
}

juce::StringArray ModuleLibraryComponent::getDraggableModuleNames() const {
    juce::StringArray names;
    for (const auto& entry : entries)
        if (entry.kind == RowKind::Module)
            names.add(entry.text);
    return names;
}

juce::String ModuleLibraryComponent::getSectionForModule(const juce::String& moduleName) const {
    juce::String currentHeader;
    for (const auto& entry : entries) {
        if (entry.isHeader)
            currentHeader = entry.text;
        else if (entry.text == moduleName)
            return currentHeader;
    }
    return {};
}

juce::String ModuleLibraryComponent::getEntryText(int index) const {
    return (index >= 0 && index < (int)entries.size()) ? entries[index].text : juce::String();
}

int ModuleLibraryComponent::getFirstDraggableEntryIndex() const {
    for (const auto& row : buildRows())
        if (isDraggableEntry(row.entryIndex))
            return row.entryIndex;
    return -1;
}

int ModuleLibraryComponent::getRowCentreY(int entryIndex) const {
    for (const auto& row : buildRows())
        if (row.entryIndex == entryIndex)
            return row.y + row.height / 2;
    return -1;
}

int ModuleLibraryComponent::getEntryIndexAt(int contentY) const {
    for (const auto& row : buildRows())
        if (contentY >= row.y && contentY < row.y + row.height)
            return row.entryIndex;
    return -1;
}

int ModuleLibraryComponent::getEntryIndexAtComponentY(int y) const {
    if (isInPinnedChrome(y))
        return -1;
    return getEntryIndexAt(y + scrollOffset);
}

bool ModuleLibraryComponent::isDraggableEntry(int index) const {
    if (index < 0 || index >= (int)entries.size())
        return false;
    const auto kind = entries[(size_t)index].kind;
    return kind == RowKind::Module || kind == RowKind::Snippet || kind == RowKind::Plugin;
}

bool ModuleLibraryComponent::isActionEntry(int index) const {
    return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind == RowKind::Action;
}

bool ModuleLibraryComponent::isHeaderEntry(int index) const {
    return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind == RowKind::Header;
}

bool ModuleLibraryComponent::isSubHeaderEntry(int index) const {
    return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind == RowKind::SubHeader;
}

synth::PluginIdentity ModuleLibraryComponent::identityForEntry(const Entry& entry) {
    synth::PluginIdentity identity;
    identity.format = entry.detail;
    identity.name = entry.text;
    identity.uid = entry.pluginUid;
    return identity;
}

juce::String ModuleLibraryComponent::tooltipForEntry(int index) const {
    const auto& entry = entries[(size_t)index];
    switch (entry.kind) {
    case RowKind::Snippet:
        return snippetDescription(entry.text, entry.moduleCount);
    case RowKind::Plugin:
        return pluginDescription(entry.text, entry.detail);
    case RowKind::Action:
        return scanRowDescription(!plugins.empty());
    default:
        break;
    }
    juce::String tip = descriptionFor(entry.text);
    if (!isEntryEnabled(index))
        tip += " (already in this patch)";
    return tip;
}

void ModuleLibraryComponent::clampHoverToVisibleRow() {
    if (hoveredIndex < 0)
        return;
    for (const auto& row : buildRows())
        if (row.entryIndex == hoveredIndex)
            return;
    hoveredIndex = -1;
}

void ModuleLibraryComponent::clampKeyboardFocusToVisibleRow() {
    if (keyboardFocusedIndex < 0)
        return;
    // Unlike clampHoverToVisibleRow(), also re-checks the KIND at that index, not just
    // visibility: rebuildEntries() can shrink the entry list (a snippet delete removes its row
    // entirely, sliding every later index down), so the same numeric index can end up occupied
    // by a different, non-navigable row (typically the "No snippets yet" EmptyHint) after a
    // rebuild — that row is still visible, so a plain visibility check alone would leave
    // keyboardFocusedIndex silently pointing at it.
    if (!isKeyboardNavigableEntry(keyboardFocusedIndex)) {
        keyboardFocusedIndex = -1;
        return;
    }
    for (const auto& row : buildRows())
        if (row.entryIndex == keyboardFocusedIndex)
            return;
    keyboardFocusedIndex = -1;
}

void ModuleLibraryComponent::rebuildEntries() {
    entries.clear();

    auto addHeader = [this](const juce::String& text) {
        entries.push_back({text, true, RowKind::Header, {}, 0, {}, 0});
    };

    addHeader(kSnippetsHeader);
    if (snippets.isEmpty()) {
        // Keep the section visible when empty so the feature is discoverable at all.
        entries.push_back({"No snippets yet", false, RowKind::EmptyHint, kSnippetsHeader, 0, {}, 0});
    } else {
        for (const auto& snippet : snippets)
            entries.push_back({snippet.name, false, RowKind::Snippet, kSnippetsHeader, snippet.moduleCount, {}, 0});
    }

    struct Category {
        const char* header;
        std::vector<const char*> modules;
    };
    // One module per line: this list is the library's visible order, and letting it pack into a
    // grid turns inserting a module into a whole-block reflow instead of a one-line diff.
    // clang-format off
    static const std::vector<Category> catalogue = {
        {"Sources", {
            "Oscillator",
            "Wavetable",
            "Noise",
            "Sampler",
            "LFO",
        }},
        {"Sequencing", {
            "Sequencer",
            "Poly Sequencer",
            "MidiKeyboard",
            "Poly MIDI",
            "External MIDI",
        }},
        {"Envelopes & Control", {
            "ADSR",
            "Envelope Follower",
            "VCA",
        }},
        {"Filters", {
            "Filter",
            "Parametric EQ",
        }},
        {"Modulation FX", {
            "Chorus",
            "Phaser",
            "Flanger",
            "Distortion",
            "Ring Modulator",
            "Bitcrusher",
            "Pitch Shifter",
        }},
        {"Time FX", {
            "Delay",
            "Reverb",
        }},
        {"Dynamics", {
            "Compressor",
            "Limiter",
            "Gate",
        }},
        {"Utility", {
            "Macros",
            "Sample & Hold",
            "Comparator",
            "Voice Mixer",
            "Math",
        }},
        // Singletons — a patch holds at most one of each, so these rows grey out once the
        // canvas already has them (see isModuleAvailable).
        {"I/O", {
            "Audio Input",
            "Audio Output",
        }},
    };
    // clang-format on

    for (const auto& category : catalogue) {
        addHeader(category.header);
        for (const auto* moduleName : category.modules)
            entries.push_back({moduleName, false, RowKind::Module, category.header, 0, {}, 0});
    }

    // Plugins last: it is the only section whose contents come from outside this app, it is
    // empty until the user asks for a scan, and keeping it at the bottom means adding it did not
    // move a single existing row.
    addHeader(kPluginsHeader);
    entries.push_back({kScanPluginsRowText, false, RowKind::Action, kPluginsHeader, 0, {}, 0});

    // Sub-grouped by format (VST3, AudioUnit, …) so a big scan doesn't read as one undifferentiated
    // blob. Groups sort alphabetically by format name, rows inside a group by plugin name —
    // sorted here outright rather than trusting the caller's order (PluginScanService happens
    // to hand the list name-sorted, but setPlugins() makes no such promise). One sub-label per
    // format — even a single-format library still gets one, so the section always reads the
    // same way rather than special-casing the common case.
    std::vector<synth::PluginIdentity> sortedPlugins = plugins;
    std::sort(sortedPlugins.begin(), sortedPlugins.end(),
              [](const synth::PluginIdentity& a, const synth::PluginIdentity& b) {
                  return a.format != b.format ? a.format < b.format : a.name < b.name;
              });
    juce::String currentFormat;
    bool haveFormat = false;
    for (const auto& plugin : sortedPlugins) {
        if (!haveFormat || plugin.format != currentFormat) {
            entries.push_back({plugin.format, false, RowKind::SubHeader, kPluginsHeader, 0, {}, 0});
            currentFormat = plugin.format;
            haveFormat = true;
        }
        entries.push_back({plugin.name, false, RowKind::Plugin, kPluginsHeader, 0, plugin.format, plugin.uid});
    }
}
