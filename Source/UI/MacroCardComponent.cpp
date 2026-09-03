#include "MacroCardComponent.h"
#include "GraphEditor.h"

namespace {
// A small fixed palette rather than a full colour picker — P8-12 is visual/organisational
// scope, and a macro's colour only needs to be distinguishable at a glance, not infinitely
// tunable. Matches the swatch-row idiom used elsewhere in this app's settings UI.
const std::vector<juce::Colour>& macroColourPalette() {
    static const std::vector<juce::Colour> palette{
        juce::Colour(0xff5a7dff), juce::Colour(0xffff6b6b), juce::Colour(0xff51cf66), juce::Colour(0xffffa94d),
        juce::Colour(0xffcc5de8), juce::Colour(0xff22b8cf), juce::Colour(0xfffcc419), juce::Colour(0xff868e96),
    };
    return palette;
}
} // namespace

MacroCardComponent::MacroCardComponent(GraphEditor& owner, juce::String macroId)
    : owner(owner)
    , macroId(std::move(macroId)) {
    setInterceptsMouseClicks(true, false);
}

MacroCardComponent::~MacroCardComponent() { finishRename(false); }

void MacroCardComponent::paint(juce::Graphics& g) {
    const auto* macro = owner.getMacros().find(macroId);
    if (macro == nullptr)
        return;

    auto bounds = getLocalBounds().toFloat();
    const bool selected = owner.isMacroSelected(macroId);

    g.setColour(macro->colour.withAlpha(0.22f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(selected ? juce::Colours::white : macro->colour);
    g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, selected ? 2.0f : 1.5f);

    if (nameEditor != nullptr)
        return; // editor covers the name; member-count line still reads fine underneath

    auto textArea = getLocalBounds().reduced(10, 6);
    auto titleRow = textArea.removeFromTop(20);
    titleRow.removeFromRight(28); // leave room for the expand chevron
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    g.drawText(macro->name.isNotEmpty() ? macro->name : "Macro", titleRow, juce::Justification::centredLeft);

    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    const int n = (int)macro->members.size();
    g.drawText(juce::String(n) + (n == 1 ? " module" : " modules"), textArea, juce::Justification::bottomLeft);

    // Expand chevron — a filled triangle rather than a text glyph, so there's no non-ASCII
    // string literal to trip check-nonascii-literals.test.sh and no themed icon asset to add for
    // one small affordance.
    const auto chevronBounds = getExpandButtonBounds();
    juce::Path chevron;
    chevron.addTriangle(chevronBounds.getX() + 3.0f, chevronBounds.getY() + 7.0f, chevronBounds.getRight() - 3.0f,
                        chevronBounds.getY() + 7.0f, chevronBounds.getCentreX(), chevronBounds.getBottom() - 5.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.fillPath(chevron);
}

juce::Rectangle<float> MacroCardComponent::getExpandButtonBounds() const {
    constexpr float kSize = 20.0f;
    constexpr float kMargin = 8.0f;
    return juce::Rectangle<float>(getWidth() - kMargin - kSize, kMargin, kSize, kSize);
}

void MacroCardComponent::mouseDown(const juce::MouseEvent& e) {
    owner.commitAnyOpenTitleRename();
    finishRename(true);

    if (e.mods.isRightButtonDown()) {
        if (!owner.isMacroSelected(macroId))
            owner.selectMacro(macroId, false);
        showContextMenu();
        return;
    }

    if (getExpandButtonBounds().contains(e.position)) {
        owner.setMacroCollapsed(macroId, false);
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCommandDown()) {
        owner.selectMacro(macroId, true);
        return;
    }

    if (!owner.isMacroSelected(macroId))
        owner.selectMacro(macroId, false);

    dragStartPosition = getPosition();
    bodyDragActive = true;
    dragger.startDraggingComponent(this, e);
    owner.beginMacroCardDrag(macroId);
}

void MacroCardComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!bodyDragActive)
        return;
    dragger.dragComponent(this, e, nullptr);
    owner.dragMacroCardBy(macroId, getPosition() - dragStartPosition);
    if (auto* p = getParentComponent())
        p->repaint();
}

void MacroCardComponent::mouseUp(const juce::MouseEvent&) {
    if (!bodyDragActive)
        return;
    bodyDragActive = false;

    if (getPosition() != dragStartPosition)
        owner.finalizeMacroCardDrag(macroId, getPosition());
    else
        owner.cancelMacroCardDrag(macroId);
}

void MacroCardComponent::mouseDoubleClick(const juce::MouseEvent&) { owner.setMacroCollapsed(macroId, false); }

void MacroCardComponent::beginRename() {
    finishRename(false);
    const auto* macro = owner.getMacros().find(macroId);
    if (macro == nullptr)
        return;

    nameEditor = std::make_unique<juce::TextEditor>("macroNameEditor");
    nameEditor->setMultiLine(false);
    nameEditor->setReturnKeyStartsNewLine(false);
    nameEditor->setJustification(juce::Justification::centredLeft);
    nameEditor->setText(macro->name, juce::dontSendNotification);
    nameEditor->setBounds(getLocalBounds().reduced(8, 4));
    nameEditor->onReturnKey = [this] { finishRename(true); };
    nameEditor->onEscapeKey = [this] { finishRename(false); };
    nameEditor->onFocusLost = [this] { finishRename(true); };
    addAndMakeVisible(*nameEditor);
    nameEditor->selectAll();
    nameEditor->grabKeyboardFocus();
}

void MacroCardComponent::finishRename(bool commit) {
    if (nameEditor == nullptr)
        return;

    // Detach FIRST — destroying the editor moves focus off it, which fires onFocusLost, which
    // would otherwise re-enter here (same ordering as ModuleComponent::finishTitleRename).
    auto editor = std::move(nameEditor);
    const juce::String typed = editor->getText();
    editor.reset();

    if (commit)
        owner.renameMacro(macroId, typed.trim());
    repaint();
}

juce::PopupMenu MacroCardComponent::buildColourSubMenu() {
    // A small named swatch list rather than a full juce::ColourSelector — P8-12 is
    // visual/organisational scope, and a macro's colour only needs to be distinguishable at a
    // glance. juce::PopupMenu items don't support arbitrary icons without a custom LookAndFeel
    // hook, so the swatches are named rather than drawn.
    juce::PopupMenu colourMenu;
    static const char* names[] = {"Blue", "Red", "Green", "Orange", "Purple", "Cyan", "Yellow", "Grey"};
    const auto& palette = macroColourPalette();
    juce::Component::SafePointer<MacroCardComponent> safeThis(this);
    for (size_t idx = 0; idx < palette.size() && idx < 8; ++idx) {
        auto colour = palette[idx];
        colourMenu.addItem(names[idx], [safeThis, colour] {
            if (safeThis != nullptr)
                safeThis->owner.setMacroColour(safeThis->macroId, colour);
        });
    }
    return colourMenu;
}

void MacroCardComponent::showContextMenu() {
    juce::Component::SafePointer<MacroCardComponent> safeThis(this);
    const auto* macro = owner.getMacros().find(macroId);
    if (macro == nullptr)
        return;

    juce::PopupMenu m;
    m.addItem(macro->collapsed ? "Expand" : "Collapse", [safeThis] {
        if (safeThis != nullptr)
            safeThis->owner.setMacroCollapsed(safeThis->macroId,
                                              !safeThis->owner.getMacros().find(safeThis->macroId)->collapsed);
    });
    m.addItem("Rename...", [safeThis] {
        if (safeThis != nullptr)
            safeThis->beginRename();
    });
    m.addSubMenu("Change Colour", buildColourSubMenu());
    m.addSeparator();
    m.addItem("Save as Snippet...", [safeThis] {
        if (safeThis != nullptr && safeThis->owner.onSaveSnippetRequested)
            safeThis->owner.onSaveSnippetRequested();
    });
    m.addItem("Ungroup", [safeThis] {
        if (safeThis != nullptr)
            safeThis->owner.ungroupSelection();
    });
    m.addSeparator();
    m.addItem("Delete Macro && Modules", [safeThis] {
        if (safeThis != nullptr)
            safeThis->owner.deleteMacroAndMembers(safeThis->macroId);
    });

    m.showMenuAsync(juce::PopupMenu::Options());
}
