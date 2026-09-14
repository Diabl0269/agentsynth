// ModuleLibraryComponent.cpp -- construction/destruction and the snippet/plugin data setters,
// plus the shared row-activation entry point (activateRow) and the enable/availability gate
// (isEntryEnabled) that both the mouse and keyboard paths call through.
#include "ModuleLibraryComponent.h"

ModuleLibraryComponent::ModuleLibraryComponent() {
    // The flat entry list is rebuilt rather than assigned literally, because it now has to be
    // regenerated whenever the snippet list changes. The module catalogue itself lives in
    // rebuildEntries() — add new modules there.
    rebuildEntries();
    snapSectionProgressToTargets();
    setMouseCursor(juce::MouseCursor::NormalCursor);
    // Prevent the parent component from grabbing keyboard focus when clicked (e.g. on the
    // collapse-all strip). Without this, clicking anywhere in the parent would cause the
    // searchEditor child to gain focus, clearing its placeholder text.
    setMouseClickGrabsKeyboardFocus(false);
    // T159: makes grabKeyboardFocus() on THIS component (the "library" focus region's root)
    // succeed deterministically. juce::Component::grabKeyboardFocusInternal only takes the
    // focus itself when wantsKeyboardFocusFlag is set; otherwise it descends into children by
    // Y/X position (NOT by which child wants focus), which is a fragile thing to depend on for
    // a container whose row layout can change. Orthogonal to setMouseClickGrabsKeyboardFocus
    // above — that flag is checked first and separately, so mouse clicks on the panel's own
    // custom-painted rows still never steal focus from the search box.
    setWantsKeyboardFocus(true);

    // addChildComponent, not addAndMakeVisible: updateScrollBar() owns the visibility, so the bar
    // only appears once the rows actually outgrow the panel.
    addChildComponent(verticalScrollBar);
    verticalScrollBar.setAutoHide(false);
    verticalScrollBar.addListener(this);

    searchEditor.setMultiLine(false);
    searchEditor.setReturnKeyStartsNewLine(false);
    searchEditor.setEscapeAndReturnKeysConsumed(true);
    searchEditor.setSelectAllWhenFocused(true);
    searchEditor.setJustification(juce::Justification::centredLeft);
    searchEditor.setBorder(juce::BorderSize<int>(0));
    searchEditor.setIndents(6, 0);
    searchEditor.setFont(juce::Font(juce::FontOptions(13.0f)));
    searchEditor.setTooltip("Filter the library by module, snippet, or category name.");
    searchEditor.onTextChange = [this] { applySearchQuery(searchEditor.getText()); };
    searchEditor.onEscapeKey = [this] {
        if (searchEditor.getText().isNotEmpty())
            setSearchText({});
    };
    addAndMakeVisible(searchEditor);
    applySearchEditorColours();
    // T160: intercepts Up/Down/Return ahead of the editor's own keyPressed — see
    // Source/UI/CLAUDE.md-adjacent notes below on why a KeyListener rather than an override is
    // required here (ComponentPeer::handleKeyPress runs a component's key LISTENERS before its
    // own keyPressed, and TextEditor::moveCaretUp/Down unconditionally return true for a
    // single-line editor via moveCaretToStartOfLine/EndOfLine, so an override on this component
    // would never see them while real focus sits in searchEditor). Deliberately does NOT
    // intercept Left/Right or Tab: Left/Right must keep moving the text caret while the user is
    // still editing the query, and Tab must keep bubbling untouched to MainComponent's
    // focusNextRegion cycle (see keyPressed(KeyPress, Component*) below).
    searchEditor.addKeyListener(this);
}

ModuleLibraryComponent::~ModuleLibraryComponent() {
    // The animator's callbacks capture `this`, so it must not outlive us.
    if (vblankUpdater.has_value())
        collapseAnim.stop(*vblankUpdater);
    searchEditor.removeKeyListener(this);
    verticalScrollBar.removeListener(this);
    // helpCallOutBox_ holds a non-owning reference to *helpPopup_ (see the help-popover
    // section below) — end its modal state before either member starts tearing down, purely
    // defensive (juce::Component's own destructor already detaches from any modal manager).
    if (helpCallOutBox_)
        helpCallOutBox_->exitModalState(0);
}

void ModuleLibraryComponent::setSnippets(const juce::Array<synth::SnippetInfo>& newSnippets) {
    snippets.clearQuick();
    snippets.addArray(newSnippets);
    rebuildEntries();
    clampHoverToVisibleRow();
    clampKeyboardFocusToVisibleRow();
    updateScrollBar();
    repaint();
}

void ModuleLibraryComponent::setPlugins(const std::vector<synth::PluginIdentity>& newPlugins) {
    plugins = newPlugins;
    rebuildEntries();
    clampHoverToVisibleRow();
    clampKeyboardFocusToVisibleRow();
    updateScrollBar();
    repaint();
}

void ModuleLibraryComponent::activateRow(int index) {
    if (index < 0 || index >= (int)entries.size())
        return;

    const auto& entry = entries[(size_t)index];
    if (entry.kind == RowKind::Action) {
        if (onScanPluginsRequested)
            onScanPluginsRequested();
        return;
    }
    if (entry.kind == RowKind::Plugin && onPluginActivated) {
        onPluginActivated(identityForEntry(entry));
        return;
    }
    if (entry.kind == RowKind::Module) {
        if (isEntryEnabled(index) && onModuleActivated)
            onModuleActivated(entry.text);
        return;
    }
    if (entry.kind == RowKind::Snippet && onSnippetActivated)
        onSnippetActivated(entry.text);
}

synth::PluginIdentity ModuleLibraryComponent::getPluginIdentity(int index) const {
    if (index < 0 || index >= (int)entries.size() || entries[(size_t)index].kind != RowKind::Plugin)
        return {};
    return identityForEntry(entries[(size_t)index]);
}

bool ModuleLibraryComponent::isEntryEnabled(int index) const {
    if (!isDraggableEntry(index))
        return false;
    if (entries[(size_t)index].kind != RowKind::Module)
        return true;
    return !isModuleAvailable || isModuleAvailable(entries[(size_t)index].text);
}
