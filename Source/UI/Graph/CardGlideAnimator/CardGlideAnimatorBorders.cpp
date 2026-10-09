// CardGlideAnimatorBorders.cpp
//
// The open macro's border in the delete and undo animation (docs/layout/animation.md "Delete and undo animation"): when
// a delete removes a macro together with all its modules, the dashed border, name chip and port strips shrink away
// with the module cards instead of vanishing; an undo grows them back with the cards. The border is drawn from the
// data captured before the change (Border), since the macro itself is gone by the time the ghost is armed.

#include "CardGlideAnimator.h"

#include "UI/Graph/MacroFoldAnimator/MacroFoldAnimator.h"
#include "UI/Layout/ReducedMotion.h"

#include <algorithm>

void CardGlideAnimator::noteMacroBorders() {
    if (depth_ == 0 || !hooks_.borders || !borders_.empty() || !canAnimate())
        return;
    borders_ = hooks_.borders();
}

bool CardGlideAnimator::isBorderHeld(const juce::String& macroId) const noexcept {
    const auto key = borderKey(macroKey(macroId));
    return std::any_of(items_.begin(), items_.end(), [key](const Item& it) {
        return it.border != nullptr && it.kind == Kind::Enter && !it.grown && it.nodeUid == key;
    });
}

int CardGlideAnimator::borderGhostCount() const noexcept {
    return static_cast<int>(
        std::count_if(items_.begin(), items_.end(), [](const Item& it) { return it.border != nullptr; }));
}

// A macro the change removed, none of whose modules survives, exits; a macro an undo brought back, whose modules all
// enter, enters. A macro that merely lost its border (ungrouped, collapsed) is not a ghost: its modules stay.
void CardGlideAnimator::armBorderGhosts(const std::vector<Entry>& now, bool& anyExit, bool& anyEnter) {
    if (!hooks_.borders || (borders_.empty() && !restoring_))
        return; // only a delete or an undo asks, so the other scopes pay nothing
    const auto visibleNow = [&now](uint32_t uid) {
        return std::any_of(now.begin(), now.end(), [uid](const Entry& e) {
            return e.nodeUid == uid && e.comp != nullptr && e.comp->isVisible();
        });
    };
    const auto addItem = [this](Kind kind, const Border& b) {
        Item item;
        item.kind = kind;
        item.nodeUid = borderKey(b.key);
        item.from = item.to = b.hull.getUnion(b.chip);
        item.border = std::make_shared<const Border>(b);
        items_.push_back(std::move(item));
    };

    const auto after = hooks_.borders();
    for (const auto& b : borders_) {
        if (!b.open || b.hull.isEmpty() ||
            std::any_of(after.begin(), after.end(), [&b](const Border& a) { return a.key == b.key; }))
            continue;
        if (std::none_of(b.members.begin(), b.members.end(), visibleNow)) {
            addItem(Kind::Exit, b);
            anyExit = true;
        }
    }
    if (!restoring_)
        return;
    for (const auto& b : after) {
        if (!b.open || b.hull.isEmpty() || preexistingMacros_.count(b.key) != 0 || b.members.empty())
            continue;
        const bool allEnter = std::all_of(b.members.begin(), b.members.end(), [this](uint32_t uid) {
            return std::any_of(items_.begin(), items_.end(),
                               [uid](const Item& it) { return it.kind == Kind::Enter && it.nodeUid == uid; });
        });
        if (allEnter) {
            addItem(Kind::Enter, b);
            anyEnter = true;
        }
    }
}

// The border as the canvas draws it (GraphEditorCables.cpp paintExpandedMacroHulls, GraphEditorMacroHullStrips.cpp),
// scaled about its centre and faded together.
void CardGlideAnimator::paintBorder(juce::Graphics& g, const Border& b, float scale, float alpha) {
    if (scale <= 0.0f || alpha <= 0.0f)
        return;
    juce::Graphics::ScopedSaveState state(g);
    const auto centre = b.hull.toFloat().getCentre();
    g.addTransform(juce::AffineTransform::scale(scale, scale, centre.x, centre.y));

    const auto hull = b.hull.toFloat();
    constexpr float kHullRadius = 10.0f;
    juce::Path left, right;
    left.addRoundedRectangle(hull.getX(), hull.getY(), static_cast<float>(b.inWidth), hull.getHeight(), kHullRadius,
                             kHullRadius, true, false, true, false);
    right.addRoundedRectangle(hull.getRight() - static_cast<float>(b.outWidth), hull.getY(),
                              static_cast<float>(b.outWidth), hull.getHeight(), kHullRadius, kHullRadius, false, true,
                              false, true);
    g.setColour(b.stripFill.withMultipliedAlpha(alpha));
    g.fillPath(left);
    g.fillPath(right);

    MacroFoldAnimator::paintDashedBorder(g, hull, b.colour.withMultipliedAlpha(alpha), false);

    const auto chip = b.chip.toFloat();
    g.setColour(b.colour.withAlpha(0.85f).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(chip, 6.0f);
    g.setColour(juce::Colours::white.withMultipliedAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText(b.name.isNotEmpty() ? b.name : juce::String("Macro"), chip.withLeft(chip.getX() + 12.0f),
               juce::Justification::centred, false);
}
