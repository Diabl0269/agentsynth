#include "ToolbarComponent.h"
#include "Theme/AppLookAndFeel.h"

// ---------------------------------------------------------------------------
void ToolbarComponent::setButtons(std::array<juce::DrawableButton*, NumSlots> btns) { buttons_ = btns; }

// ---------------------------------------------------------------------------
// Sub-group membership per Slot, used both to space groups apart in layoutButtons() and to
// find the group boundaries paint() draws separator hairlines at. Matches the Slot enum's
// documented grouping (ToolbarComponent.h:21-22): left [Library]|[New,Save,Load]|[Settings]|
// [Undo,Redo]|[AutoArrange]; right [ToggleMinimap,ToggleModMatrix,ToggleAiPanel,ToggleTimeline]|
// [ToggleTheme].
namespace {
int groupOf(int slot) {
    switch (slot) {
    case ToolbarComponent::Library:
        return 0;
    case ToolbarComponent::New:
    case ToolbarComponent::Save:
    case ToolbarComponent::Load:
        return 1;
    case ToolbarComponent::Settings:
        return 2;
    case ToolbarComponent::Undo:
    case ToolbarComponent::Redo:
        return 3;
    case ToolbarComponent::AutoArrange:
        return 4;
    case ToolbarComponent::ToggleMinimap:
    case ToolbarComponent::ToggleModMatrix:
    case ToolbarComponent::ToggleAiPanel:
    case ToolbarComponent::ToggleTimeline:
        return 5;
    case ToolbarComponent::ToggleTheme:
        return 6;
    default:
        return -1;
    }
}
} // namespace

// ---------------------------------------------------------------------------
void ToolbarComponent::layoutButtons(juce::Rectangle<int> bounds) {
    // At or below the breakpoint (== the enforced minimum window width) the wide-mode
    // labelled buttons no longer fit, so collapse to icon-only.
    narrowMode_ = bounds.getWidth() <= narrowThreshold_;

    // Wide-mode preferred widths (icon + text). Narrow mode collapses everything to 32 px.
    static constexpr float kNarrowPref = 32.0f;
    const std::array<float, NumSlots> widePref = {
        96.0f,  // Library
        88.0f,  // New
        112.0f, // Save
        116.0f, // Load
        96.0f,  // Settings
        72.0f,  // Undo
        72.0f,  // Redo
        120.0f, // AutoArrange
        108.0f, // ToggleMinimap ("Hide Minimap"/"Show Minimap")
        104.0f, // ToggleModMatrix
        92.0f,  // ToggleAiPanel
        112.0f, // ToggleTimeline ("Hide Timeline"/"Show Timeline")
        110.0f  // ToggleTheme
    };

    auto prefFor = [&](int slot) { return narrowMode_ ? kNarrowPref : widePref[(size_t)slot]; };

    juce::FlexBox fb;
    fb.flexDirection = juce::FlexBox::Direction::row;
    fb.alignItems = juce::FlexBox::AlignItems::center;

    // Small gap inserted between sub-groups (on top of each button's own margin) so related
    // actions read as clusters rather than one flat row.
    static constexpr float kGroupGap = 12.0f;

    // Left group: Library, Save, Load, Settings, Undo, Redo, AutoArrange.
    // Invisible buttons (e.g. ToggleTimeline while the "Show timeline" preference is off) yield
    // their slot entirely rather than leaving a reserved gap.
    int lastGroup = -1;
    for (int slot = Library; slot <= AutoArrange; ++slot)
        if (buttons_[(size_t)slot] != nullptr && buttons_[(size_t)slot]->isVisible()) {
            if (lastGroup != -1 && groupOf(slot) != lastGroup)
                fb.items.add(juce::FlexItem().withWidth(kGroupGap));
            lastGroup = groupOf(slot);
            fb.items.add(juce::FlexItem(*buttons_[(size_t)slot])
                             .withMinWidth(0.0f)
                             .withWidth(prefFor(slot))
                             .withHeight((float)bounds.getHeight())
                             .withMargin(juce::FlexItem::Margin(3.0f)));
        }

    // Flexible spacer pushes the right group to the far edge.
    fb.items.add(juce::FlexItem().withFlex(1.0f));

    // Right group: ToggleMinimap, ToggleModMatrix, ToggleAiPanel, ToggleTimeline, ToggleTheme.
    lastGroup = -1;
    for (int slot = ToggleMinimap; slot <= ToggleTheme; ++slot)
        if (buttons_[(size_t)slot] != nullptr && buttons_[(size_t)slot]->isVisible()) {
            if (lastGroup != -1 && groupOf(slot) != lastGroup)
                fb.items.add(juce::FlexItem().withWidth(kGroupGap));
            lastGroup = groupOf(slot);
            fb.items.add(juce::FlexItem(*buttons_[(size_t)slot])
                             .withMinWidth(0.0f)
                             .withWidth(prefFor(slot))
                             .withHeight((float)bounds.getHeight())
                             .withMargin(juce::FlexItem::Margin(3.0f)));
        }

    fb.performLayout(bounds.toFloat());
}

// ---------------------------------------------------------------------------
void ToolbarComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    auto* lnf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());
    if (lnf != nullptr)
        g.fillAll(lnf->getTheme().colors.bg0);
    else
        g.fillAll(juce::Colour(0xff0B0D10));

    if (lnf == nullptr)
        return; // headless test LnF: no themed border token available, skip separators.

    // Draw a hairline at each sub-group boundary, using the buttons' own post-layout bounds so
    // this pass never disagrees with layoutButtons()'s FlexItem order. Only consider slots whose
    // button is non-null && isVisible() — the same guard layoutButtons() already applies — so a
    // null ToggleTimeline (SYNTH_ENABLE_TIMELINE=OFF) can't anchor a separator on stale bounds.
    g.setColour(lnf->getTheme().colors.border.withAlpha(0.5f));

    // Sections match the two layoutButtons() loops (left: Library..AutoArrange, right:
    // ToggleMinimap..ToggleTheme) — the boundary BETWEEN sections is the existing withFlex(1.0f)
    // spacer, not a sub-group gap, so it is deliberately excluded here.
    auto sectionOf = [](int slot) { return slot <= AutoArrange ? 0 : 1; };

    int prevGroup = -1;
    int prevSection = -1;
    int prevRight = -1;
    for (int slot = 0; slot < NumSlots; ++slot) {
        auto* b = buttons_[(size_t)slot];
        if (b == nullptr || !b->isVisible())
            continue;

        const int group = groupOf(slot);
        const int section = sectionOf(slot);
        const auto bounds = b->getBounds();
        if (prevGroup != -1 && group != prevGroup && section == prevSection && prevRight != -1 &&
            bounds.getX() > prevRight) {
            const float midX = 0.5f * (float)(prevRight + bounds.getX());
            g.drawLine(midX, 8.0f, midX, (float)getHeight() - 8.0f, 1.0f);
        }
        prevGroup = group;
        prevSection = section;
        prevRight = bounds.getRight();
    }
}
