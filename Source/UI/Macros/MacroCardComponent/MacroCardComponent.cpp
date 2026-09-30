#include "MacroCardComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/GraphEditor/GraphEditorInternal.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

// The port-strip constants live in GraphEditorInternal.h, shared with
// MacroGroupControllerGeometry.cpp (macroCardPortLayout) rather than re-derived here.
using namespace detail;

MacroCardComponent::MacroCardComponent(GraphEditor& owner, juce::String macroId)
    : owner(owner)
    , macroId(std::move(macroId)) {
    setInterceptsMouseClicks(true, false);
    showContextMenuHook_ = [](juce::PopupMenu& menu) { menu.showMenuAsync(juce::PopupMenu::Options()); };
}

MacroCardComponent::~MacroCardComponent() { finishRename(false); }

void MacroCardComponent::paint(juce::Graphics& g) {
    const auto* macro = owner.getMacros().find(macroId);
    if (macro == nullptr)
        return;

    auto bounds = getLocalBounds().toFloat();
    const bool selected = owner.getMacroController().isMacroSelected(macroId);

    const auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    static const synth::theme::Colors fallbackColors{};
    const auto& themeColors = lf != nullptr ? lf->getTheme().colors : fallbackColors;

    g.setColour(macro->colour.withAlpha(0.22f));
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(selected ? themeColors.textPrimary : macro->colour);
    g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, selected ? 2.0f : 1.5f);

    if (nameEditor != nullptr)
        return; // editor covers the name; member-count line still reads fine underneath

    auto textArea = getContentArea();
    textArea.removeFromTop(20); // the title row itself is drawn via getTitleRowBounds() below
    const auto titleRow = getTitleRowBounds();
    g.setColour(themeColors.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    g.drawText(macro->name.isNotEmpty() ? macro->name : "Macro", titleRow, juce::Justification::centredLeft);

    auto countRow = textArea.removeFromBottom(14);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.setColour(themeColors.textMuted);
    g.drawText(getModuleCountText(), countRow, juce::Justification::bottomLeft);

    // ---- Content preview ----------------------------
    // A collapsed macro used to be an opaque box with nothing but a name and a count. Draw a
    // small "minimap" of the member module boxes — their LIVE canvas bounds (still tracking, even
    // hidden — see syncMacroCards), scaled to fit the strip left between the title and the count
    // line, one filled rect per member coloured by module CATEGORY so it echoes what expanding
    // the macro would show. Drawn in the middle column between the two port strips.
    const auto previewArea = textArea.reduced(0, 2);
    if (!previewArea.isEmpty()) {
        const auto members = owner.getMacroController().macroMemberPreviews(macroId);
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

    paintPortStrips(g, *macro, themeColors);

    const auto chevronBounds = getExpandButtonBounds();

    // Bypass/mute indeterminate indicator (docs/macros/ports.md#bypass-and-mute): "mixed-state
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
    paintToggleBadge(getToggleBadgeBounds(false), themeColors.warning,
                     owner.getMacroController().macroBypassState(macro->id));
    paintToggleBadge(getToggleBadgeBounds(true), themeColors.error,
                     owner.getMacroController().macroMuteState(macro->id));

    // Expand chevron — a filled triangle rather than a text glyph, so there's no non-ASCII
    // string literal to trip check-nonascii-literals.test.sh and no themed icon asset to add for
    // one small affordance.
    juce::Path chevron;
    chevron.addTriangle(chevronBounds.getX() + 3.0f, chevronBounds.getY() + 7.0f, chevronBounds.getRight() - 3.0f,
                        chevronBounds.getY() + 7.0f, chevronBounds.getCentreX(), chevronBounds.getBottom() - 5.0f);
    g.setColour(themeColors.textPrimary);
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

juce::Rectangle<int> MacroCardComponent::getContentArea() const {
    // The middle column between the two port strips: title, member preview and count live here.
    const auto [inW, outW] = owner.getMacroController().macroCardStripWidths(macroId);
    return getLocalBounds().withTrimmedLeft(inW).withTrimmedRight(outW).reduced(10, 6);
}

juce::Rectangle<int> MacroCardComponent::getTitleRowBounds() const {
    auto textArea = getContentArea();
    auto titleRow = textArea.removeFromTop(20);
    // Reserve room for the expand chevron AND both bypass/mute badges (getToggleBadgeBounds) —
    // keeps a long macro name's text from painting under either, and keeps the double-click
    // rename zone off them too. Derived from getToggleBadgeBounds' own outer edge rather than a
    // second copy of the "28 + 2 slots" arithmetic, so the two can never drift apart.
    // With the fixed-width strips the chevron and badges sit in the output strip, beyond this column, so only the
    // part that actually intrudes into the title row is reserved.
    const int reserve = juce::jmax(0, titleRow.getRight() - (int)getToggleBadgeBounds(false).getX());
    titleRow.removeFromRight(reserve);
    return titleRow;
}

void MacroCardComponent::mouseDown(const juce::MouseEvent& e) {
    owner.commitAnyOpenTitleRename();
    finishRename(true);

    if (e.mods.isRightButtonDown()) {
        // Captured BEFORE the reselect below, which otherwise destroys any external batch
        // (or a partial subset of this macro's own members, picked for a targeted "Remove
        // Selection from Macro") the user chose before right-clicking this card — see
        // buildMacroMenu's own comment on addCandidateSelection. Only reselect when there was
        // NO prior selection at all: any non-empty selection, whether it's an addable outside
        // module or a subset of members meant for removal, must survive untouched so its
        // border keeps showing the user what the menu is about to act on.
        const auto priorSelection = owner.getSelectedNodes();
        if (priorSelection.empty())
            owner.getMacroController().selectMacro(macroId, false);
        showContextMenu(priorSelection);
        return;
    }

    // A hovered
    // port's 'x' deletes it and a side's '+' opens the add-port choice menu — both checked at the
    // SAME precedence getExpandButtonBounds() already has below (before shift/cmd multi-select
    // and the drag-arm fallback), so neither steals a card body drag or an expand click. The 'x'
    // re-runs macroCardPortForPoint() rather than trusting hoveredPortUuid_ alone, since a
    // mouseDown with no preceding mouseMove (a real click landing where the mouse already
    // rested, or a test driving mouseDown directly with no mouseMove first) must never delete a
    // jack it never actually hovered.
    if (hoveredPortUuid_.has_value()) {
        const auto hit = owner.getMacroController().macroCardPortForPoint(macroId, e.getPosition());
        if (hit.has_value() && hit->nodeUuid == *hoveredPortUuid_) {
            owner.getMacroController().deleteMacroPortManually(macroId, *hoveredPortUuid_);
            // A quick double-click must not delete TWO
            // ports: the delete reflows macroCardPortLayout(), so the very next jack slides
            // under the still-resting cursor and immediately shows ITS OWN 'x', which the second
            // click of the double-click then hit. Clearing the hover here is not enough on its
            // own (the next mouseMove would just re-arm it at the same pixel); suppressing hover
            // at this exact click position until the mouse genuinely moves away from it is what
            // actually closes the gap — see mouseMove()'s own comment.
            hoveredPortUuid_.reset();
            suppressHoverAtPosition_ = e.getPosition();
            return;
        }
    }
    for (const bool isInput : {true, false}) {
        if (getAddPortButtonBounds(isInput).contains(e.position)) {
            auto menu = buildAddPortMenu(isInput);
            showContextMenuHook_(menu);
            return;
        }
        if (getRemovePortButtonBounds(isInput).contains(e.position)) {
            removeBottomPort(isInput);
            return;
        }
    }

    if (getExpandButtonBounds().contains(e.position)) {
        owner.getMacroController().setMacroCollapsed(macroId, false);
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCommandDown()) {
        owner.getMacroController().selectMacro(macroId, true);
        return;
    }

    if (!owner.getMacroController().isMacroSelected(macroId))
        owner.getMacroController().selectMacro(macroId, false);

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

    owner.getMacroController().setMacroCollapsed(macroId, false);
}

void MacroCardComponent::mouseMove(const juce::MouseEvent& e) {
    // A position suppressed by a just-completed 'x' delete (mouseDown's own
    // comment on suppressHoverAtPosition_) stays suppressed until a mouseMove reports a
    // DIFFERENT position — a mouseMove at the identical position (JUCE can dispatch one even with
    // no real movement, e.g. as part of the click plumbing itself) must not re-arm hover on
    // whatever port the reflow just slid underneath the resting cursor. Any position that
    // genuinely differs means the mouse moved for real, so normal hover tracking resumes.
    if (suppressHoverAtPosition_.has_value()) {
        if (e.getPosition() == *suppressHoverAtPosition_)
            return;
        suppressHoverAtPosition_.reset();
    }

    // The ONE place hoveredPortUuid_ is armed — macroCardPortForPoint() is the SAME hit-test the
    // drop-target path (GraphEditor::endConnectionDrag) and paint()'s hovered-'x' overlay both
    // read, so hovering, drawing and deleting can never disagree about which jack the mouse is on.
    const auto hit = owner.getMacroController().macroCardPortForPoint(macroId, e.getPosition());
    const std::optional<juce::String> newHover =
        hit.has_value() ? std::optional<juce::String>(hit->nodeUuid) : std::nullopt;
    if (newHover != hoveredPortUuid_) {
        hoveredPortUuid_ = newHover;
        repaint();
    }
}

void MacroCardComponent::mouseExit(const juce::MouseEvent&) {
    suppressHoverAtPosition_.reset(); // leaving the card is itself real movement
    if (hoveredPortUuid_.has_value()) {
        hoveredPortUuid_.reset();
        repaint();
    }
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
        owner.getMacroController().renameMacro(macroId, typed.trim());
    repaint();
}

void MacroCardComponent::showContextMenu(const std::vector<juce::AudioProcessorGraph::NodeID>& priorSelection) {
    // owner.buildMacroMenu is the ONE shared builder — this card's own right-click menu and the
    // expanded-macro hull's right-click menu (GraphEditor::mouseDown) both go through it, so they
    // cannot drift apart. This card is the one caller that overrides the
    // default "Rename..." handler: it has a real MacroCardComponent to host the nicer inline
    // TextEditor rename, which nothing else building this menu has. `priorSelection` is
    // whatever was selected right before mouseDown's own reselect — see buildMacroMenu's
    // addCandidateSelection comment.
    juce::Component::SafePointer<MacroCardComponent> safeThis(this);
    auto menu = owner.buildMacroMenu(
        macroId,
        [safeThis] {
            if (safeThis != nullptr)
                safeThis->beginRename();
        },
        &priorSelection);
    showContextMenuHook_(menu);
}

juce::String MacroCardComponent::getModuleCountText() const {
    // Counts MODULES, not graph MEMBERS (which include the auto-created port nodes for the crossing
    // cable). synth::Macro::moduleMemberCount() is the one place that
    // exclusion lives now (MacroSet.h) — every other user-facing count/list routes through it or
    // through memberIsPort(), so this can never drift back out of sync with the tooltip below.
    //
    // A plain macro with no ports reads just "2 modules". A macro that DOES have ports names the port count
    // alongside it, rather than silently hiding a real quantity: "2 modules, 2 ports" is more
    // honest than either "4 modules" or a bare "2 modules" that pretends the ports
    // aren't there.
    const auto* macro = owner.getMacros().find(macroId);
    if (macro == nullptr)
        return {};

    const int moduleCount = macro->moduleMemberCount();
    juce::String text = juce::String(moduleCount) + (moduleCount == 1 ? " module" : " modules");

    const int portCount = (int)macro->ports.size();
    if (portCount > 0)
        text << ", " << portCount << (portCount == 1 ? " port" : " ports");

    return text;
}

juce::String MacroCardComponent::getTooltip() {
    // While names are faded by zoom, or ellipsised, the tooltip carries the hovered port's full name.
    if (hoveredPortUuid_.has_value())
        for (const auto& port : owner.getMacroController().macroCardPortLayout(macroId))
            if (port.nodeUuid == *hoveredPortUuid_) {
                if (portNameAlpha() < 1.0f || portNameIsTruncated(port.name))
                    return port.name;
                break;
            }

    const auto names = owner.getMacroController().macroMemberNames(macroId);
    constexpr int kMaxNamesShown = 10;

    juce::StringArray shown;
    for (int i = 0; i < names.size() && i < kMaxNamesShown; ++i)
        shown.add(names[i]);
    if (names.size() > kMaxNamesShown)
        shown.add("+" + juce::String(names.size() - kMaxNamesShown) + " more");

    return shown.joinIntoString("\n");
}

// ---- live jack-colour preview (view-layer only; never the stored MacroPort::colour) -------
//
// Per-port, because one card draws EVERY port's jack at once: the armed entry is keyed by nodeUuid, so
// previewing one port cannot recolour its siblings. Both set/clear return whether the card actually
// moved, so GraphEditor::previewMacroPortColour can skip the repaint on an unchanged tick (the picker
// re-fires the same colour on commit) and clearMacroPortColourPreview stays a real no-op when the
// committed port was never previewed. Transient view state only -- it is never written back to
// MacroPort::colour, so dragging the selector pushes no undo step.
bool MacroCardComponent::setPortColourPreview(const juce::String& nodeUuid, juce::Colour c) {
    // Idempotent -- re-arming the same node with the same colour changes nothing, so repaint nothing.
    if (portColourPreview_ && portColourPreview_->first == nodeUuid && portColourPreview_->second == c)
        return false;
    portColourPreview_ = {nodeUuid, c};
    return true;
}

bool MacroCardComponent::clearPortColourPreview(const juce::String& nodeUuid) {
    // Only clear when this entry matches the node told to clear, so a stale clear for a different
    // port (one the picker no longer previews) does not wipe a fresh preview.
    if (!portColourPreview_ || portColourPreview_->first != nodeUuid)
        return false;
    portColourPreview_.reset();
    return true;
}

bool MacroCardComponent::hasPortColourPreviewForTest(const juce::String& nodeUuid) const {
    return portColourPreview_.has_value() && portColourPreview_->first == nodeUuid;
}

juce::Colour MacroCardComponent::resolvePortJackColour(const juce::String& nodeUuid,
                                                       const std::optional<juce::Colour>& stored,
                                                       juce::Colour kindTint) const {
    // Mirrors paint()'s branch: a preview for this node wins, else the stored colour, else the kind tint.
    if (portColourPreview_.has_value() && portColourPreview_->first == nodeUuid)
        return portColourPreview_->second;
    return stored.value_or(kindTint);
}
