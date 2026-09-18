// TimelineClipLaneSelection.cpp
//
// Selected-clip span query, panel-scoped keyboard shortcuts (keyPressed), and marquee
// begin/update/end. TimelineClipLaneArea is declared in TimelineClipLaneArea.h; sibling
// TimelineClipLane*.cpp files in this directory hold the rest of the class.

#include "TimelineClipLaneArea.h"

#include "AppUndoManager.h"
#include "ShortcutManager/ShortcutManager.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace synth::ui {

//==============================================================================
std::optional<std::pair<double, double>> TimelineClipLaneArea::getSelectedClipSpan() const {
    if (doc_ == nullptr)
        return std::nullopt;

    bool any = false;
    double start = 0.0, end = 0.0;
    for (auto id : selection_.getSelected()) {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            continue; // a selection entry the doc no longer has: skipped, never guessed at
        const double clipStart = clip->startBeat;
        const double clipEnd = clip->startBeat + clip->lengthBeats;
        start = any ? std::min(start, clipStart) : clipStart;
        end = any ? std::max(end, clipEnd) : clipEnd;
        any = true;
    }

    if (!any || !(end > start))
        return std::nullopt;
    return std::make_pair(start, end);
}

// Same panel-scoped Delete/Escape/P idiom as GraphEditor. This is only the local half of
// cross-panel key arbitration — MainComponent::resolveEditSurface decides which panel's
// keyPressed even gets called.
bool TimelineClipLaneArea::keyPressed(const juce::KeyPress& key) {
    // P = loop the selection (Cubase's locators-to-selection). Rebindable through
    // "timelineLoopSelection" but NOT a command: it is resolved right here, on the surface that
    // knows the span, so no selection (or no owner listening) returns false and the key keeps
    // whatever meaning it has elsewhere. With no ShortcutManager installed this is the hardcoded
    // bare P; with one installed an unset binding means no key at all (see setShortcutManager).
    const auto matchesLoopSelection = [this, &key] {
        const juce::KeyPress fallback('p', juce::ModifierKeys::noModifiers, 0);
        if (shortcuts_ == nullptr)
            return key == fallback;
        const auto binding = shortcuts_->getBinding("timelineLoopSelection");
        // keyPressMatches rather than == so a rebind onto a Shift-chorded symbol key survives the
        // macOS peer delivering the SHIFTED character as the key code.
        return ShortcutManager::keyPressMatches(binding, key);
    };

    if (matchesLoopSelection()) {
        const auto span = getSelectedClipSpan();
        if (!span || !onLoopRangeRequested)
            return false;
        onLoopRangeRequested(span->first, span->second);
        return true;
    }

    if (key == juce::KeyPress::escapeKey) {
        if (selection_.isEmpty())
            return false;
        selection_.clear();
        repaint();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        auto ids = selection_.getSelected();
        if (ids.empty() || doc_ == nullptr)
            return false;

        auto mutate = [this, ids] {
            for (auto id : ids)
                doc_->removeClip(id);
        };
        if (undoManager_)
            undoManager_->recordTimelineChange(*doc_, mutate);
        else
            mutate();

        selection_.clear();
        repaint();
        return true;
    }

    return false;
}

//==============================================================================
void TimelineClipLaneArea::beginMarquee(juce::Point<int> anchor, bool additive) {
    dragMode_ = DragMode::Marquee;
    marqueeAdditive_ = additive;
    marqueeAnchor_ = anchor;
    marqueeRect_ = juce::Rectangle<int>(anchor, anchor);
    marqueeBaseSelection_ = additive ? selection_.getSelected() : std::vector<synth::ClipId>{};

    // A non-additive marquee starts from nothing, so dragging over no clip deselects.
    if (!additive)
        selection_.clear();
    repaint();
}

void TimelineClipLaneArea::updateMarquee(juce::Point<int> current) {
    marqueeRect_ = juce::Rectangle<int>(marqueeAnchor_, current);
    auto hits = clipHitTestMarquee(marqueeRect_, collectClipRects());

    if (marqueeAdditive_) {
        std::set<synth::ClipId> merged(marqueeBaseSelection_.begin(), marqueeBaseSelection_.end());
        merged.insert(hits.begin(), hits.end());
        selection_.setSelection({merged.begin(), merged.end()});
    } else {
        selection_.setSelection(hits);
    }
    repaint();
}

void TimelineClipLaneArea::endMarquee() {
    marqueeRect_ = {};
    marqueeBaseSelection_.clear();
    marqueeAdditive_ = false;
    repaint();
}

} // namespace synth::ui
