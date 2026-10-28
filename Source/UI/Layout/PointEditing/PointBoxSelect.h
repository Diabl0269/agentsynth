// PointBoxSelect.h -- the box-select gesture both point editors share: press on empty space, drag a rectangle, the
// dots inside it become the selection (docs/control/point-editing.md). It owns the rectangle, the pending-click state
// and the marquee paint; the editor says which dots lie inside and applies the result.
#pragma once

#include <algorithm>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth::ui {

// The editor-independent half. The owner outlives it and calls paintMarquee() from its own paint(); it repaints the
// owner itself. Message thread only.
class PointBoxGesture {
public:
    explicit PointBoxGesture(juce::Component& owner)
        : owner_(owner) {}

    // What ending a press did: nothing was pressed, a box ended, or a press that never moved (deselect).
    enum class Release { none, box, click };

    // A plain press only arms; an additive one (Shift/Cmd/Ctrl) starts the box at once.
    void press(juce::Point<int> pos, bool additive);
    Release release();
    void cancel();
    bool isActive() const noexcept { return active_; }
    juce::Rectangle<float> box() const { return box_.toFloat(); }
    void paintMarquee(juce::Graphics& g) const;

protected:
    // Follows the pointer; true while a box is being drawn.
    bool advance(juce::Point<int> pos);

private:
    void begin();

    juce::Component& owner_;
    bool pending_ = false;
    bool active_ = false;
    juce::Point<int> anchor_;
    juce::Rectangle<int> box_;
};

// The gesture over one kind of dot id (a beat, a node index). `Id` needs operator== and a default constructor.
template <typename Id>
class PointBoxSelect : public PointBoxGesture {
public:
    using PointBoxGesture::PointBoxGesture;
    // Which ids lie inside a rectangle in the owner's coordinates (an empty rectangle is never asked).
    using IdsInBox = std::function<std::vector<Id>(juce::Rectangle<float>)>;

    // `selected` is the set an additive box keeps.
    void press(juce::Point<int> pos, bool additive, const std::vector<Id>& selected) {
        PointBoxGesture::press(pos, additive);
        base_ = additive ? selected : std::vector<Id>{};
    }
    // The selection the box now makes (kept ids plus those inside), or nothing when no box is being drawn.
    std::optional<std::vector<Id>> dragTo(juce::Point<int> pos, const IdsInBox& inside) {
        if (!advance(pos))
            return std::nullopt;
        std::vector<Id> next = base_;
        if (!box().isEmpty())
            for (const Id& id : inside(box()))
                next.push_back(id);
        return next;
    }

private:
    std::vector<Id> base_;
};

} // namespace synth::ui
