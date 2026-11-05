#pragma once

// Badge.h -- the one small tag that says where a saved thing came from: Yours, Factory, Pack or Marketplace. Lists
// that paint their own rows (the Sounds view, the snippet list, the shape picker) call paintBadge(); a place that
// wants a component uses Badge. Every colour is a theme token, so it reads in light and dark themes alike.
#include "UI/Theme/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

enum class BadgeKind { Yours, Factory, Pack, Marketplace };

/** The badge's text and its spoken name: "Yours", "Factory", "Pack", "Marketplace". */
const char* badgeText(BadgeKind kind);

/** The height every badge has; its width follows its text. */
constexpr int kBadgeHeight = 16;

/** The width the badge of `kind` needs, in whole pixels. */
int badgeWidth(BadgeKind kind);

struct BadgeColours {
    juce::Colour fill; ///< translucent, so the badge takes the row it sits on
    juce::Colour border;
    juce::Colour text;
};

BadgeColours badgeColours(const synth::theme::Theme& theme, BadgeKind kind);

/** Paints the badge filling `area` (a badge is badgeWidth() by kBadgeHeight). */
void paintBadge(juce::Graphics& g, const synth::theme::Theme& theme, BadgeKind kind, juce::Rectangle<float> area);

/** The badge as a component for a place that lays out children. Not a focus stop; its name is its text. */
class Badge : public juce::Component {
public:
    explicit Badge(BadgeKind kind = BadgeKind::Yours);

    void setKind(BadgeKind kind);
    BadgeKind getKind() const noexcept { return kind_; }
    /** The width and height the badge wants. */
    juce::Rectangle<int> getIdealBounds() const;

    void paint(juce::Graphics& g) override;

private:
    BadgeKind kind_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Badge)
};

} // namespace synth::ui
