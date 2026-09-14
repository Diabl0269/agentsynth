// ModuleLibraryInput.cpp -- mouse events (hover/press/drag/click), keyboard navigation
// (Up/Down/Left/Right/Enter, including the searchEditor KeyListener interception) and starting a
// drag-and-drop session for a row.
#include "ModuleLibraryComponent.h"

void ModuleLibraryComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (verticalScrollBar.isVisible())
        verticalScrollBar.mouseWheelMove(e.getEventRelativeTo(&verticalScrollBar), wheel);
    else
        juce::Component::mouseWheelMove(e, wheel);
}

void ModuleLibraryComponent::mouseMove(const juce::MouseEvent& e) {
    const bool wasTopStripHovered = topStripHovered;
    const bool wasHelpButtonHovered = helpButtonHovered;
    topStripHovered = isInTopStrip(e.y);
    // The help button shares the strip's row, so its own hover is a sub-check within it —
    // shares getHelpButtonBounds() with paint() and mouseDown() (see that method's comment).
    helpButtonHovered = topStripHovered && getHelpButtonBounds().contains(e.getPosition());

    const int entryUnderMouse = getEntryIndexAtComponentY(e.y);
    // Only interactive rows can be hovered; headers and hints clamp to -1.
    const int newIndex = isInteractiveEntry(entryUnderMouse) ? entryUnderMouse : -1;

    if (newIndex != hoveredIndex || topStripHovered != wasTopStripHovered ||
        helpButtonHovered != wasHelpButtonHovered) {
        hoveredIndex = newIndex;

        // Update tooltip: the shared TooltipWindow (owned by MainComponent) reads
        // this component's tooltip string on each hover. Setting it here on hover
        // change means each draggable row surfaces its per-module description.
        if (helpButtonHovered) {
            setTooltip("Open a quick guide to using the module library.");
        } else if (topStripHovered) {
            setTooltip("Collapse or expand every category in the library.");
        } else if (hoveredIndex >= 0) {
            setTooltip(tooltipForEntry(hoveredIndex));
        } else {
            setTooltip({});
        }

        repaint();
    }

    // Update cursor: grab hand for draggable items, pointing hand for the clickable chrome.
    // An unavailable row is not draggable, so it must not advertise the grab hand.
    if (hoveredIndex >= 0 && isDraggableEntry(hoveredIndex) && isEntryEnabled(hoveredIndex))
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else if (topStripHovered || isHeaderEntry(entryUnderMouse) || isSubHeaderEntry(entryUnderMouse) ||
             isActionEntry(hoveredIndex))
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ModuleLibraryComponent::mouseExit(const juce::MouseEvent&) {
    if (hoveredIndex != -1 || topStripHovered || helpButtonHovered) {
        hoveredIndex = -1;
        topStripHovered = false;
        helpButtonHovered = false;
        setTooltip({});
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ModuleLibraryComponent::mouseDown(const juce::MouseEvent& e) {
    pressedIndex = -1;

    if (isInTopStrip(e.y)) {
        if (getHelpButtonBounds().contains(e.getPosition())) {
            showHelpPopover();
            return;
        }
        toggleAllSections();
        return;
    }

    const int index = getEntryIndexAtComponentY(e.y);
    if (index < 0 || index >= (int)entries.size())
        return;

    const auto& entry = entries[(size_t)index];

    if (entry.kind == RowKind::Header) {
        toggleSection(entry.text);
        return;
    }

    if (entry.kind == RowKind::EmptyHint)
        return;

    if (entry.kind == RowKind::SubHeader) {
        toggleSection(subsectionKey(entry.section, entry.text));
        return;
    }

    // Click-activated rows (the scan command, and plugin rows, which support BOTH click-to-add
    // and drag-to-place) defer to mouseUp/mouseDrag. Module and snippet rows keep starting their
    // drag on mouse-down, which is what every existing drag test drives.
    // TRUE right button, deliberately not isPopupMenu(): on macOS JUCE defines
    // popupMenuClickModifier as (rightButtonModifier | ctrlModifier), so isPopupMenu() is also
    // true for Ctrl+LEFT-click — and Ctrl is the insert-between drag modifier. Testing
    // isPopupMenu() here meant a Ctrl-held press on a library row never started a drag at all,
    // so Ctrl+drag-from-library could not reach the canvas. Right-click still suppresses drags.
    if (entry.kind == RowKind::Action || entry.kind == RowKind::Plugin) {
        if (!pressSuppressesRowDrag(e.mods))
            pressedIndex = index;
        return;
    }

    if (entry.kind == RowKind::Snippet && pressSuppressesRowDrag(e.mods)) {
        const auto name = entry.text;
        juce::PopupMenu m;
        m.addItem("Delete Snippet", [this, name] {
            if (onSnippetDeleteRequested)
                onSnippetDeleteRequested(name);
        });
        m.showMenuAsync(juce::PopupMenu::Options());
        return;
    }

    if (pressSuppressesRowDrag(e.mods))
        return;

    // An unavailable row must not start a drag at all — accepting one and then dropping it on
    // the floor reads as the canvas being broken rather than the module being unavailable.
    if (!isEntryEnabled(index))
        return;

    startDragForEntry(index);
}

void ModuleLibraryComponent::mouseDrag(const juce::MouseEvent& e) {
    // Only the click-activated kinds get here with a pending press; everything else already
    // started its drag on mouse-down.
    if (pressedIndex < 0)
        return;
    if (entries[(size_t)pressedIndex].kind != RowKind::Plugin)
        return; // the scan command is a button, not a drag source
    if (e.getDistanceFromDragStart() < kDragStartThresholdPx)
        return;

    const int index = pressedIndex;
    pressedIndex = -1;
    startDragForEntry(index);
}

void ModuleLibraryComponent::mouseUp(const juce::MouseEvent& e) {
    const int index = pressedIndex;
    pressedIndex = -1;
    if (index < 0 || e.mouseWasDraggedSinceMouseDown())
        return;
    activateRow(index);
}

bool ModuleLibraryComponent::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::upKey))
        return moveKeyboardFocus(-1);
    if (key.isKeyCode(juce::KeyPress::downKey))
        return moveKeyboardFocus(1);
    if (key.isKeyCode(juce::KeyPress::leftKey))
        return handleFoldKey(true);
    if (key.isKeyCode(juce::KeyPress::rightKey))
        return handleFoldKey(false);
    if (key == juce::KeyPress::returnKey)
        return handleEnterKey();
    return false;
}

bool ModuleLibraryComponent::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) {
    if (originatingComponent != &searchEditor)
        return false;
    if (key.isKeyCode(juce::KeyPress::upKey))
        return moveKeyboardFocus(-1);
    if (key.isKeyCode(juce::KeyPress::downKey))
        return moveKeyboardFocus(1);
    // Only once a row is already keyboard-focused — otherwise Return keeps its normal
    // (consumed, no-op) TextEditor behaviour, since the query has nothing to insert yet.
    if (key == juce::KeyPress::returnKey && keyboardFocusedIndex >= 0)
        return handleEnterKey();
    return false;
}

bool ModuleLibraryComponent::isKeyboardNavigableEntry(int index) const {
    return index >= 0 && index < (int)entries.size() && entries[(size_t)index].kind != RowKind::EmptyHint;
}

std::vector<int> ModuleLibraryComponent::navigableEntryIndices() const {
    std::vector<int> result;
    for (const auto& row : buildRows())
        if (isKeyboardNavigableEntry(row.entryIndex))
            result.push_back(row.entryIndex);
    return result;
}

bool ModuleLibraryComponent::moveKeyboardFocus(int delta) {
    const auto navigable = navigableEntryIndices();
    if (navigable.empty())
        return false;

    int pos = -1;
    for (size_t i = 0; i < navigable.size(); ++i) {
        if (navigable[i] == keyboardFocusedIndex) {
            pos = (int)i;
            break;
        }
    }

    const int next = (pos < 0) ? (delta > 0 ? 0 : (int)navigable.size() - 1)
                               : juce::jlimit(0, (int)navigable.size() - 1, pos + delta);
    keyboardFocusedIndex = navigable[(size_t)next];
    scrollKeyboardFocusIntoView();
    repaint();
    return true;
}

bool ModuleLibraryComponent::handleFoldKey(bool collapse) {
    if (keyboardFocusedIndex < 0 || keyboardFocusedIndex >= (int)entries.size())
        return false;
    const auto& entry = entries[(size_t)keyboardFocusedIndex];
    if (entry.kind == RowKind::Header) {
        setSectionCollapsed(entry.text, collapse);
        return true;
    }
    if (entry.kind == RowKind::SubHeader) {
        setSectionCollapsed(subsectionKey(entry.section, entry.text), collapse);
        return true;
    }
    return false;
}

bool ModuleLibraryComponent::handleEnterKey() {
    if (keyboardFocusedIndex < 0)
        return false;
    activateRow(keyboardFocusedIndex);
    return true;
}

void ModuleLibraryComponent::scrollKeyboardFocusIntoView() {
    if (keyboardFocusedIndex < 0)
        return;
    for (const auto& row : buildRows()) {
        if (row.entryIndex != keyboardFocusedIndex)
            continue;
        const int viewportTop = kPinnedChromeHeight;
        const int viewportBottom = getHeight();
        const int rowTop = row.y - scrollOffset;
        const int rowBottom = rowTop + row.height;
        if (rowTop < viewportTop)
            setScrollOffset(scrollOffset - (viewportTop - rowTop));
        else if (rowBottom > viewportBottom)
            setScrollOffset(scrollOffset + (rowBottom - viewportBottom));
        return;
    }
}

void ModuleLibraryComponent::setKeyboardFocusedIndexForTest(int index) {
    keyboardFocusedIndex = isKeyboardNavigableEntry(index) ? index : -1;
    clampKeyboardFocusToVisibleRow();
    repaint();
}

void ModuleLibraryComponent::startDragForEntry(int index) {
    const auto& entry = entries[(size_t)index];

    juce::String payload = entry.text;
    if (entry.kind == RowKind::Snippet)
        payload = synth::SnippetManager::payloadForName(entry.text);
    else if (entry.kind == RowKind::Plugin)
        payload = identityForEntry(entry).toDragPayload();

    juce::Image dragImage(juce::Image::ARGB, 150, 30, true);
    juce::Graphics dg(dragImage);
    dg.setColour(juce::Colours::white);
    dg.setFont(juce::Font(juce::FontOptions(16.0f)));
    dg.drawText(entry.text, dragImage.getBounds(), juce::Justification::centred, false);

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
        container->startDragging(payload, this, dragImage);
}
