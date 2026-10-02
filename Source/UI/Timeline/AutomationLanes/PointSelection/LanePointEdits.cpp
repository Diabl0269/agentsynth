#include "LanePointEdits.h"
#include "AppUndoManager.h"
#include <algorithm>

namespace synth::ui {

bool commitPointEdit(const LaneEditTarget& target, const std::vector<double>& removeBeats,
                     const std::vector<LaneBreakpoint>& add) {
    if (target.doc == nullptr || !target.lane.isValid())
        return false;
    auto* doc = target.doc;
    const auto lane = target.lane;
    const auto before = doc->getRevision();
    auto mutate = [doc, lane, &removeBeats, &add] { doc->editBreakpoints(lane, removeBeats, add); };
    if (target.undo != nullptr)
        target.undo->recordTimelineChange(*doc, mutate);
    else
        mutate();
    return doc->getRevision() != before;
}

LanePointClipboard copyPoints(const std::vector<LaneBreakpoint>& selected) {
    LanePointClipboard clipboard;
    if (selected.empty())
        return clipboard;
    double earliest = selected.front().beat;
    for (const auto& p : selected)
        earliest = std::min(earliest, p.beat);
    for (const auto& p : selected)
        clipboard.entries.push_back({p.beat - earliest, p.value, p.tension, p.curve});
    return clipboard;
}

std::vector<LaneBreakpoint> pastedPoints(const LanePointClipboard& clipboard, double anchorBeat, double minValue,
                                         double maxValue) {
    std::vector<LaneBreakpoint> out;
    out.reserve(clipboard.entries.size());
    for (const auto& e : clipboard.entries)
        out.push_back({anchorBeat + e.offset, juce::jlimit(minValue, maxValue, e.value), e.tension, e.curve});
    return out;
}

} // namespace synth::ui
