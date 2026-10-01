#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The arrow at the left of a track header's name row that folds the track's automation lanes out
// under it (pointing right while folded, down while open). A real Tab stop: Return/Space toggle
// it, its screen-reader name and tooltip say what a press does next ("Show Bass automation").
// A right-click never toggles; it asks the row for its context menu instead.
class TrackFoldArrow : public juce::Button {
public:
    static constexpr int kSize = 16; // square hit area, px

    TrackFoldArrow();

    /** Updates the glyph, name and tooltip. `trackName` is the row's current name; a folded arrow
     *  also says how many lanes it would show, since a track row has no room for a count badge. */
    void setState(bool expanded, const juce::String& trackName, int laneCount = 0);
    bool isExpanded() const noexcept { return expanded_; }

    /** A right-click on the arrow; null = ignored. */
    std::function<void()> onPopupMenuRequested;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    bool expanded_ = false;
};

/** "1 lane" / "N lanes". */
juce::String laneCountBadgeText(int laneCount);
/** The badge's width for `laneCount` at the badge font, px. */
int laneCountBadgeWidth(int laneCount);
/** The muted, outlined "N lanes" pill on the Unassigned automation section header. */
void paintLaneCountBadge(juce::Graphics& g, juce::Rectangle<int> area, int laneCount, const juce::Component& owner);

} // namespace synth::ui
