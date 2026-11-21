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
    // Where the box was pressed. A view that scrolls under a held box moves its anchor with the content, so the box
    // keeps its pressed corner on what was pressed; the next advance() redraws it.
    juce::Point<int> anchor() const noexcept { return anchor_; }
    void setAnchor(juce::Point<int> pos) noexcept { anchor_ = pos; }
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

    // Where an id is drawn, in the owner's coordinates (optional: it only ranks ids that enter in the same move).
    using Where = std::function<juce::Point<float>(const Id&)>;

    // `selected` is the set an additive box keeps.
    void press(juce::Point<int> pos, bool additive, const std::vector<Id>& selected) {
        PointBoxGesture::press(pos, additive);
        base_ = additive ? selected : std::vector<Id>{};
        entered_.clear();
        seen_ = false;
    }
    // The selection the box now makes (kept ids plus those inside), or nothing when no box is being drawn.
    std::optional<std::vector<Id>> dragTo(juce::Point<int> pos, const IdsInBox& inside, const Where& where = {}) {
        if (!advance(pos))
            return std::nullopt;
        std::vector<Id> next = base_;
        std::vector<Id> now;
        if (!box().isEmpty())
            now = inside(box());
        trackEntries(now, where, seen_ ? pos.toFloat() : anchor().toFloat());
        seen_ = seen_ || !now.empty();
        for (const Id& id : now)
            next.push_back(id);
        return next;
    }
    // The id that entered the box last: the cursor a box selection leaves (the dot added last, whichever side the box
    // grew from). Ids that enter in one move are ranked by distance, the nearest last: to the press point for the first
    // set, to the pointer after that. Nothing while the box holds none.
    std::optional<Id> lastEntered() const {
        return entered_.empty() ? std::nullopt : std::optional<Id>(entered_.back());
    }

private:
    void trackEntries(const std::vector<Id>& now, const Where& where, juce::Point<float> from) {
        entered_.erase(std::remove_if(entered_.begin(), entered_.end(),
                                      [&](const Id& id) { return std::find(now.begin(), now.end(), id) == now.end(); }),
                       entered_.end());
        std::vector<Id> fresh;
        for (const Id& id : now)
            if (std::find(entered_.begin(), entered_.end(), id) == entered_.end())
                fresh.push_back(id);
        if (where)
            std::stable_sort(fresh.begin(), fresh.end(), [&](const Id& a, const Id& b) {
                return where(a).getDistanceFrom(from) > where(b).getDistanceFrom(from);
            });
        entered_.insert(entered_.end(), fresh.begin(), fresh.end());
    }

    std::vector<Id> base_;
    std::vector<Id> entered_; // ids in the box now, in the order they entered
    bool seen_ = false;       // the box has held something since the press
};

} // namespace synth::ui
