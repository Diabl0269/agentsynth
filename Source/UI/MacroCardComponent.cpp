#include "MacroCardComponent.h"
#include "GraphEditor.h"
#include "Theme/AppLookAndFeel.h"

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
    textArea.removeFromTop(20); // the title row itself is drawn via getTitleRowBounds() below
    const auto titleRow = getTitleRowBounds();
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    g.drawText(macro->name.isNotEmpty() ? macro->name : "Macro", titleRow, juce::Justification::centredLeft);

    auto countRow = textArea.removeFromBottom(14);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    const int n = (int)macro->members.size();
    g.drawText(juce::String(n) + (n == 1 ? " module" : " modules"), countRow, juce::Justification::bottomLeft);

    // ---- Content preview (Fix 6/P8-12 follow-up) ----
    // A collapsed macro used to be an opaque box with nothing but a name and a count. Draw a
    // small "minimap" of the member module boxes — their LIVE canvas bounds (still tracking, even
    // hidden — see syncMacroCards), scaled to fit the strip left between the title and the count
    // line, one filled rect per member coloured by module CATEGORY so it echoes what expanding
    // the macro would show. Deliberately drawn INSIDE the existing kMacroCardHeight footprint:
    // Macro::bounds is persisted, so growing the card would give already-saved macros a second
    // size on the same canvas.
    const auto previewArea = textArea.reduced(0, 2);
    if (!previewArea.isEmpty()) {
        const auto members = owner.macroMemberPreviews(macroId);
        juce::Rectangle<int> unionBounds;
        for (const auto& member : members)
            unionBounds = unionBounds.isEmpty() ? member.bounds : unionBounds.getUnion(member.bounds);

        if (!unionBounds.isEmpty()) {
            const float scale = juce::jmin(previewArea.getWidth() / (float)unionBounds.getWidth(),
                                           previewArea.getHeight() / (float)unionBounds.getHeight());
            const float scaledW = unionBounds.getWidth() * scale;
            const float scaledH = unionBounds.getHeight() * scale;
            const float offsetX = previewArea.getX() + (previewArea.getWidth() - scaledW) * 0.5f;
            const float offsetY = previewArea.getY() + (previewArea.getHeight() - scaledH) * 0.5f;

            for (const auto& member : members) {
                juce::Rectangle<float> box((member.bounds.getX() - unionBounds.getX()) * scale + offsetX,
                                           (member.bounds.getY() - unionBounds.getY()) * scale + offsetY,
                                           juce::jmax(2.0f, member.bounds.getWidth() * scale),
                                           juce::jmax(2.0f, member.bounds.getHeight() * scale));
                g.setColour(owner.categoryPreviewColour(member.category).withAlpha(0.85f));
                g.fillRoundedRectangle(box, 1.5f);
            }
        }
    }
    // ---- End content preview ----

    // ---- Port jacks (P8-15c, T141: docs/macros.md §7 item 4) ----
    // One jack per configured port — inputs down the left edge, outputs down the right, from the
    // SAME owner.macroCardPortLayout() that this card's own hit-testing (endConnectionDrag's jack
    // check) and buildVisibleCables()'s boundary-cable anchoring both read, so the drawn dot is
    // never anywhere those two disagree about. Colour matches ModuleComponent::paint's own jack
    // convention verbatim (its comment: "Audio-signal jacks (MIDI in/out) -> audioWire;
    // mod-capable input/output jacks -> accent") — audioWire for a MIDI port's jack, accent for an
    // AudioCV one, not the *Wire-at-a-paint-site the CABLE-colour invariant forbids
    // (Source/UI/CLAUDE.md): a JACK dot is not a cable, and this follows the one jack-painting
    // site in the codebase that already makes this exact call.
    const auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& themeColors = lf != nullptr ? lf->getTheme().colors : fallbackColors;
    {
        for (const auto& port : owner.macroCardPortLayout(macro->id)) {
            g.setColour(port.kind == synth::MacroPortKind::Midi ? themeColors.audioWire : themeColors.accent);
            g.fillEllipse((float)port.jackPos.x - 5.0f, (float)port.jackPos.y - 5.0f, 10.0f, 10.0f);

            // Port name (founder-review fix F2, item 3/docs/macros.md §7 item 4: "it's not shown
            // on the module UI... it should be presented"): left-aligned inside the left edge for
            // an input, right-aligned inside the right edge for an output — mirroring the docked
            // widget's own left/right convention (§5.3/§5.4) so an expanded and collapsed macro
            // read a port's name the same way. Elided (drawFittedText, one line) if the card is
            // too narrow for the full name.
            if (port.name.isNotEmpty()) {
                g.setColour(juce::Colours::white.withAlpha(0.85f));
                g.setFont(juce::Font(juce::FontOptions(9.5f)));
                const int labelW = juce::jmax(20, getWidth() / 2 - 16);
                auto labelArea =
                    port.isInput ? juce::Rectangle<int>(port.jackPos.x + 8, port.jackPos.y - 7, labelW, 14)
                                 : juce::Rectangle<int>(port.jackPos.x - 8 - labelW, port.jackPos.y - 7, labelW, 14);
                g.drawFittedText(port.name, labelArea,
                                 port.isInput ? juce::Justification::centredLeft : juce::Justification::centredRight,
                                 1);
            }
        }
    }

    const auto chevronBounds = getExpandButtonBounds();

    // Bypass/mute indeterminate indicator (P8-15d, T142, docs/macros.md §5.6): "mixed-state
    // members show an indeterminate indicator." Two fixed badge slots sit just left of the expand
    // chevron -- mute nearer the chevron, bypass further out -- so their positions never shift
    // depending on which is actually drawn (a jumping badge would be worse than a missing one).
    // AllOff draws nothing (absence == off, matching an un-pressed per-module bypass/mute button);
    // AllOn is a solid dot; Mixed is a half-filled dot, the usual tri-state-checkbox idiom for
    // "some, not all" -- read fresh from owner.macroBypassState/macroMuteState on every paint, the
    // same live-query approach macroCardPortLayout above already uses, so this can never show a
    // stale state (GraphEditor::setMacroBypassed/setMacroMuted repaint this card explicitly after
    // every fan-out for exactly that reason -- unlike a member's own header button, this card has
    // no parameter listener of its own to notice the change).
    auto paintToggleBadge = [&g](juce::Rectangle<float> bounds, juce::Colour colour,
                                 GraphEditor::MacroToggleState state) {
        if (state == GraphEditor::MacroToggleState::AllOff)
            return;

        g.setColour(colour);
        if (state == GraphEditor::MacroToggleState::AllOn) {
            g.fillEllipse(bounds);
            return;
        }

        // Mixed: fill only the left half, then outline the whole circle.
        {
            juce::Graphics::ScopedSaveState clipGuard(g);
            g.reduceClipRegion(juce::Rectangle<int>((int)bounds.getX(), (int)bounds.getY(),
                                                    (int)(bounds.getWidth() * 0.5f) + 1, (int)bounds.getHeight() + 1));
            g.fillEllipse(bounds);
        }
        g.drawEllipse(bounds, 1.2f);
    };

    // colors.warning is the bypass family (ModMatrixComponent's own bypass toggle uses it);
    // colors.error is documented as "error / mute" on Theme::Colors itself.
    paintToggleBadge(getToggleBadgeBounds(false), themeColors.warning, owner.macroBypassState(macro->id));
    paintToggleBadge(getToggleBadgeBounds(true), themeColors.error, owner.macroMuteState(macro->id));

    // Expand chevron — a filled triangle rather than a text glyph, so there's no non-ASCII
    // string literal to trip check-nonascii-literals.test.sh and no themed icon asset to add for
    // one small affordance.
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

juce::Rectangle<float> MacroCardComponent::getToggleBadgeBounds(bool mute) const {
    const auto chevron = getExpandButtonBounds();
    const float y = chevron.getCentreY() - kToggleBadgeSize * 0.5f;
    // The mute (inner) slot sits directly left of the chevron; the bypass (outer) slot sits
    // directly left of THAT slot, whether or not either is actually drawn (see paint()'s comment
    // on why the slots are fixed rather than compacted).
    const float innerSlotX = chevron.getX() - kToggleBadgeGap - kToggleBadgeSize;
    const float x = mute ? innerSlotX : innerSlotX - kToggleBadgeGap - kToggleBadgeSize;
    return juce::Rectangle<float>(x, y, kToggleBadgeSize, kToggleBadgeSize);
}

juce::Rectangle<int> MacroCardComponent::getTitleRowBounds() const {
    auto textArea = getLocalBounds().reduced(10, 6);
    auto titleRow = textArea.removeFromTop(20);
    // Reserve room for the expand chevron AND both bypass/mute badges (getToggleBadgeBounds) —
    // keeps a long macro name's text from painting under either, and keeps the double-click
    // rename zone off them too. Derived from getToggleBadgeBounds' own outer edge rather than a
    // second copy of the "28 + 2 slots" arithmetic, so the two can never drift apart.
    const int reserve = getWidth() - (int)getToggleBadgeBounds(false).getX();
    titleRow.removeFromRight(reserve);
    return titleRow;
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
    // dragMacroCardBy is the one repaint call for this gesture (via GraphEditor::repaintCanvas,
    // which also invalidates the cable cache so boundary cables track this card mid-drag — see
    // its own comment for why a bare getParentComponent()->repaint() here would leave them stale).
    owner.dragMacroCardBy(macroId, getPosition() - dragStartPosition);
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

void MacroCardComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    // Double-click on the title row renames in place — the same affordance ModuleComponent gives
    // its own title. Anywhere else on the card still expands, as before.
    if (getTitleRowBounds().contains(e.getPosition())) {
        // mouseDown already armed a card drag (dragStartPosition/bodyDragActive/dragger.
        // startDraggingComponent/owner.beginMacroCardDrag) before this second press resolves.
        // Opening the inline editor here — rather than expanding, which used to make the whole
        // card (and its stuck drag state) go away — leaves this card alive, so the armed drag
        // must be cancelled explicitly or the next drag anywhere moves this macro instead of
        // whatever was actually grabbed. Mirrors the equivalent fix on the hull-chip path
        // (GraphEditor::mouseDoubleClick's macroChipDragId handling).
        if (bodyDragActive) {
            owner.cancelMacroCardDrag(macroId);
            bodyDragActive = false;
        }
        beginRename();
        return;
    }

    owner.setMacroCollapsed(macroId, false);
}

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

void MacroCardComponent::showContextMenu() {
    // owner.buildMacroMenu is the ONE shared builder — this card's own right-click menu and the
    // expanded-macro hull's right-click menu (GraphEditor::mouseDown) both go through it, so they
    // cannot drift apart (Fix 4/P8-12 follow-up). This card is the one caller that overrides the
    // default "Rename..." handler: it has a real MacroCardComponent to host the nicer inline
    // TextEditor rename, which nothing else building this menu has.
    juce::Component::SafePointer<MacroCardComponent> safeThis(this);
    owner
        .buildMacroMenu(macroId,
                        [safeThis] {
                            if (safeThis != nullptr)
                                safeThis->beginRename();
                        })
        .showMenuAsync(juce::PopupMenu::Options());
}

juce::String MacroCardComponent::getTooltip() {
    const auto names = owner.macroMemberNames(macroId);
    constexpr int kMaxNamesShown = 10;

    juce::StringArray shown;
    for (int i = 0; i < names.size() && i < kMaxNamesShown; ++i)
        shown.add(names[i]);
    if (names.size() > kMaxNamesShown)
        shown.add("+" + juce::String(names.size() - kMaxNamesShown) + " more");

    return shown.joinIntoString("\n");
}
