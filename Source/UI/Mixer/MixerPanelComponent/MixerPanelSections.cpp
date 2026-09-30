// Concern: MixerPanelComponent's shared section layout -- relaying layout changes to every column
// and the toolbar, growing the host while a divider drag needs room, and persisting the sections.
#include "MixerPanelComponent.h"

#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

void MixerPanelComponent::wireSectionLayout() {
    sectionLayout_.onGeometryChanged = [this] { onSectionGeometryChanged(); };
    sectionLayout_.onAppearanceChanged = [this] { onSectionAppearanceChanged(); };
    sectionLayout_.onCommitted = [this] { onSectionLayoutCommitted(); };
}

// Loaded once per store: the heights are app-wide, like the bottom dock's own height, so the docked
// panel and the "both places" mirror read the same keys. The side pane's open state and width load here
// too, under the Mixer's own tab key.
void MixerPanelComponent::setSettingsStore(juce::PropertiesFile* settings) {
    settings_ = settings;
    if (settings_ != nullptr)
        sectionLayout_.loadFrom(*settings_);
    sidePane_.setPersistence(settings_, "mixer");
}

int MixerPanelComponent::requiredPanelHeight() const noexcept {
    return sectionLayout_.requiredColumnHeight() + MixerPanelToolbar::kHeight;
}

bool MixerPanelComponent::contentScrollsVertically() const {
    const bool hostCanGrow = canGrowHost && canGrowHost();
    return !hostCanGrow && requiredPanelHeight() > getHeight();
}

// A divider drag takes space from the fader first; once the fader would drop under its minimum, the
// drag keeps going by asking the host (the bottom dock, or the Own panel) for the difference. The
// host clamps to its own limit, so the next step re-measures rather than assuming the growth
// happened. Shrinking never shrinks the host: the space goes back to the fader. A host that cannot
// grow at all (a detached window) is handled by resized() scrolling the content instead.
//
// Re-showing a hidden section (its toolbar toggle or its strip) fits the host the same way a newly
// shown mixer does -- growHostToFitSections().
void MixerPanelComponent::onSectionGeometryChanged() {
    bool reshown = false;
    for (size_t i = 0; i < lastHidden_.size(); ++i) {
        const bool hidden = sectionLayout_.isHidden((MixerSection)(int)i);
        reshown = reshown || (lastHidden_[i] && !hidden);
        lastHidden_[i] = hidden;
    }
    if (sectionLayout_.getDraggingDivider() >= 0 && canGrowHost && canGrowHost() && growHost) {
        const int shortfall = requiredPanelHeight() - getHeight();
        if (shortfall > 0) {
            grewHostThisDrag_ = true;
            growHost(shortfall, false);
        }
    } else if (reshown) {
        growHostToFitSections();
    }
    resized();
    repaintDragBubbleStrip();
}

void MixerPanelComponent::repaintDragBubbleStrip() { repaint(0, viewport_.getY(), 72, viewport_.getHeight()); }

// Only grows, by what the sections still need beside a kMinFaderHeight fader; the host clamps to
// its own limit (3/4 of the window for the dock). Skipped while the panel has no height yet (not
// laid out, or mid-slide at 0) and for a host that cannot grow (a detached window scrolls instead).
void MixerPanelComponent::growHostToFitSections() {
    if (getHeight() <= 0 || !(canGrowHost && canGrowHost()) || !growHost)
        return;
    const int shortfall = requiredPanelHeight() - getHeight();
    if (shortfall > 0)
        growHost(shortfall, false);
}

void MixerPanelComponent::onSectionAppearanceChanged() {
    for (auto& column : stripColumns_)
        column->repaintSectionDividers();
    if (masterColumn_ != nullptr)
        masterColumn_->repaintSectionDividers();
    repaintDragBubbleStrip();
}

// Persists on gesture end only (drag release, show/hide, double-click reset), never per drag step --
// the same split the bottom dock's own resize handle uses.
void MixerPanelComponent::onSectionLayoutCommitted() {
    if (grewHostThisDrag_) {
        grewHostThisDrag_ = false;
        if (growHost)
            growHost(0, true);
    }
    toolbar_.refresh();
    if (settings_ == nullptr)
        return;
    sectionLayout_.saveTo(*settings_);
    settings_->saveIfNeeded();
}

juce::String MixerPanelComponent::getDragBubbleTextForTest() const {
    const int dragging = sectionLayout_.getDraggingDivider();
    if (dragging < 0)
        return {};
    const int rows = sectionLayout_.getRowCount((MixerSection)dragging);
    return juce::String(rows) + (rows == 1 ? " row" : " rows");
}

// The row-count bubble rides the divider being dragged, at the left edge of the first column and
// painted in the value (mono) face. The divider's y is the shared geometry every column resolves,
// offset by the panel's vertical scroll.
void MixerPanelComponent::paintDragBubble(juce::Graphics& g) const {
    const auto text = getDragBubbleTextForTest();
    if (text.isEmpty())
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto fill = laf != nullptr ? laf->getTheme().colors.surfaceHi : juce::Colour(0xff232833);
    const auto outline = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    const auto textColour = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);
    const juce::String mono = laf != nullptr ? laf->getTheme().type.monoFamily : juce::String("JetBrains Mono");
    const float size = laf != nullptr ? laf->getTheme().type.value : 10.0f;

    const auto geometry = sectionLayout_.resolve(content_.getHeight());
    const int dividerY = viewport_.getY() + geometry.dividerTop[(size_t)sectionLayout_.getDraggingDivider()] +
                         MixerSectionLayout::kDividerHeight / 2 - viewport_.getViewPositionY();
    constexpr int kBubbleWidth = 56;
    constexpr int kBubbleHeight = 14;
    const juce::Rectangle<float> bubble(6.0f, (float)(dividerY - kBubbleHeight / 2), (float)kBubbleWidth,
                                        (float)kBubbleHeight);
    g.setColour(fill);
    g.fillRoundedRectangle(bubble, 7.0f);
    g.setColour(outline);
    g.drawRoundedRectangle(bubble.reduced(0.5f), 7.0f, 1.0f);
    g.setColour(textColour);
    g.setFont(juce::Font(juce::FontOptions(mono, size, juce::Font::plain)));
    g.drawText(text, bubble, juce::Justification::centred, false);
}

} // namespace synth::ui
