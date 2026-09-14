// ModuleLibraryCollapse.cpp -- section collapse/expand state (logical, via
// collapsedSections) and its accordion animation (visual fold progress via sectionProgress /
// AnimationDriver).
#include "ModuleLibraryComponent.h"

bool ModuleLibraryComponent::isSectionCollapsed(const juce::String& header) const {
    return collapsedSections.find(header) != collapsedSections.end();
}

void ModuleLibraryComponent::setSectionCollapsed(const juce::String& header, bool collapsed) {
    const bool changed = collapsed ? collapsedSections.insert(header).second : (collapsedSections.erase(header) > 0);
    if (!changed)
        return;
    startCollapseAnimation();
    if (onCollapseStateChanged)
        onCollapseStateChanged();
    repaint();
}

bool ModuleLibraryComponent::areAllSectionsCollapsed() const {
    for (const auto& entry : entries)
        if (entry.kind == RowKind::Header && !isSectionCollapsed(entry.text))
            return false;
    return true;
}

void ModuleLibraryComponent::setAllSectionsCollapsed(bool collapsed) {
    // Starts from the CURRENT set rather than an empty one, so a subsection key (a plugin
    // format group, keyed independently via subsectionKey()) survives a collapse-all /
    // expand-all untouched — only top-level header keys are added or removed here.
    std::set<juce::String> next = collapsedSections;
    for (const auto& entry : entries) {
        if (entry.kind != RowKind::Header)
            continue;
        if (collapsed)
            next.insert(entry.text);
        else
            next.erase(entry.text);
    }
    if (next == collapsedSections)
        return;
    collapsedSections = std::move(next);
    startCollapseAnimation();
    if (onCollapseStateChanged)
        onCollapseStateChanged();
    repaint();
}

juce::StringArray ModuleLibraryComponent::getCollapsedSections() const {
    juce::StringArray result;
    for (const auto& header : collapsedSections)
        result.add(header);
    return result;
}

void ModuleLibraryComponent::setCollapsedSections(const juce::StringArray& headers) {
    collapsedSections.clear();
    for (const auto& header : headers) {
        // Persisted state arrives as newline-joined text, so an empty setting yields one blank
        // entry — never store it, or areAllSectionsCollapsed() counts a section that isn't real.
        if (header.isNotEmpty())
            collapsedSections.insert(header);
    }
    // Restore path — snap, never animate: the user did not fold anything, and animating on
    // launch would look like the sidebar collapsing by itself.
    snapSectionProgressToTargets();
    clampHoverToVisibleRow();
    clampKeyboardFocusToVisibleRow();
    updateScrollBar();
    repaint();
}

float ModuleLibraryComponent::getSectionProgress(const juce::String& header) const {
    const auto it = sectionProgress.find(header);
    return it != sectionProgress.end() ? it->second : targetProgressFor(header);
}

void ModuleLibraryComponent::setSectionProgress(const juce::String& header, float progress) {
    sectionProgress[header] = juce::jlimit(0.0f, 1.0f, progress);
    updateScrollBar();
    repaint();
}

void ModuleLibraryComponent::finishCollapseAnimation() {
    if (vblankUpdater.has_value())
        collapseAnim.stop(*vblankUpdater);
    snapSectionProgressToTargets();
    clampHoverToVisibleRow();
    clampKeyboardFocusToVisibleRow();
    updateScrollBar();
    repaint();
}

void ModuleLibraryComponent::snapSectionProgressToTargets() {
    for (const auto& entry : entries) {
        juce::String key;
        if (entry.kind == RowKind::Header)
            key = entry.text;
        else if (entry.kind == RowKind::SubHeader)
            key = subsectionKey(entry.section, entry.text);
        else
            continue;
        sectionProgress[key] = targetProgressFor(key);
    }
}

void ModuleLibraryComponent::startCollapseAnimation() {
    // No VBlank to drive frames when we're not on screen (headless tests, or a restore before
    // the window exists), so land on the final layout immediately.
    if (!isShowing()) {
        snapSectionProgressToTargets();
        clampHoverToVisibleRow();
        clampKeyboardFocusToVisibleRow();
        updateScrollBar();
        repaint();
        return;
    }

    if (!vblankUpdater.has_value())
        vblankUpdater.emplace(this);

    // Snapshot the *current* values, so retargeting mid-flight eases on from where it is
    // rather than snapping back to the start. Headers and sub-headers (plugin format groups)
    // ride the same single driver, keyed by their own collapse-state key.
    std::map<juce::String, float> from;
    std::map<juce::String, float> to;
    for (const auto& entry : entries) {
        juce::String key;
        if (entry.kind == RowKind::Header)
            key = entry.text;
        else if (entry.kind == RowKind::SubHeader)
            key = subsectionKey(entry.section, entry.text);
        else
            continue;
        from[key] = getSectionProgress(key);
        to[key] = targetProgressFor(key);
    }

    collapseAnim.start(
        *vblankUpdater, kCollapseAnimMs, synth::ui::easeInOutCubic,
        [this, from, to](float t) {
            for (const auto& [header, start] : from)
                sectionProgress[header] = start + (to.at(header) - start) * t;
            updateScrollBar();
            repaint();
        },
        [this] {
            snapSectionProgressToTargets();
            clampHoverToVisibleRow();
            clampKeyboardFocusToVisibleRow();
            updateScrollBar();
            repaint();
        });
}
