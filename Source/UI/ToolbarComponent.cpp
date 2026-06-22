#include "ToolbarComponent.h"
#include "Theme/GravisynthLookAndFeel.h"

// ---------------------------------------------------------------------------
void ToolbarComponent::setButtons(std::array<juce::DrawableButton*, NumSlots> btns) { buttons_ = btns; }

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
        104.0f, // ToggleModMatrix
        92.0f   // ToggleAiPanel
    };

    auto prefFor = [&](int slot) { return narrowMode_ ? kNarrowPref : widePref[(size_t)slot]; };

    juce::FlexBox fb;
    fb.flexDirection = juce::FlexBox::Direction::row;
    fb.alignItems = juce::FlexBox::AlignItems::center;

    // Left group: Library, Save, Load, Settings, Undo, Redo, AutoArrange.
    for (int slot = Library; slot <= AutoArrange; ++slot)
        if (buttons_[(size_t)slot] != nullptr)
            fb.items.add(juce::FlexItem(*buttons_[(size_t)slot])
                             .withMinWidth(0.0f)
                             .withWidth(prefFor(slot))
                             .withHeight((float)bounds.getHeight())
                             .withMargin(juce::FlexItem::Margin(2.0f)));

    // Flexible spacer pushes the right group to the far edge.
    fb.items.add(juce::FlexItem().withFlex(1.0f));

    // Right group: ToggleModMatrix, ToggleAiPanel.
    for (int slot = ToggleModMatrix; slot <= ToggleAiPanel; ++slot)
        if (buttons_[(size_t)slot] != nullptr)
            fb.items.add(juce::FlexItem(*buttons_[(size_t)slot])
                             .withMinWidth(0.0f)
                             .withWidth(prefFor(slot))
                             .withHeight((float)bounds.getHeight())
                             .withMargin(juce::FlexItem::Margin(2.0f)));

    fb.performLayout(bounds.toFloat());
}

// ---------------------------------------------------------------------------
void ToolbarComponent::paint(juce::Graphics& g) {
    using namespace gsynth::theme;

    if (auto* lnf = dynamic_cast<GravisynthLookAndFeel*>(&getLookAndFeel()))
        g.fillAll(lnf->getTheme().colors.bg0);
    else
        g.fillAll(juce::Colour(0xff0B0D10));
}
