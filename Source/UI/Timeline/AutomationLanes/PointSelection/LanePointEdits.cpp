#include "LanePointEdits.h"
#include "AppUndoManager.h"

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
    std::vector<double> beats, values;
    std::vector<LaneClipExtra> extras;
    for (const auto& p : selected) {
        beats.push_back(p.beat);
        values.push_back(p.value);
        extras.push_back({p.tension, p.curve});
    }
    LanePointClipboard clipboard;
    clipboard.entries = makePointClip(beats, values, extras).entries;
    return clipboard;
}

std::vector<LaneBreakpoint> pastedPoints(const LanePointClipboard& clipboard, double anchorBeat, double minValue,
                                         double maxValue) {
    const auto beats = clipPositions(clipboard, anchorBeat);
    std::vector<LaneBreakpoint> out;
    out.reserve(clipboard.entries.size());
    for (size_t i = 0; i < beats.size(); ++i) {
        const auto& e = clipboard.entries[i];
        out.push_back({beats[i], juce::jlimit(minValue, maxValue, e.value), e.extra.tension, e.extra.curve});
    }
    return out;
}

} // namespace synth::ui
