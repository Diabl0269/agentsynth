#include "SongSteps.h"

#include <algorithm>
#include <cmath>

namespace synth::songsteps {

namespace {

const char* const kKindNames[] = {"plan",    "track",    "pattern",    "sections", "automation", "module",
                                  "connect", "settings", "modulation", "fix",      "mix"};

juce::String stringOf(const juce::var& object, const char* key) { return object.getProperty(key, {}).toString(); }

int intOf(const juce::var& object, const char* key, int fallback = 0) {
    const auto v = object.getProperty(key, {});
    return (v.isInt() || v.isInt64() || v.isDouble()) ? (int)v : fallback;
}

} // namespace

juce::String stepKindToString(StepKind kind) { return kKindNames[(int)kind]; }

StepKind stepKindFromString(const juce::String& name) {
    for (int i = 0; i < (int)std::size(kKindNames); ++i)
        if (name == kKindNames[i])
            return (StepKind)i;
    return StepKind::plan;
}

juce::String stepStatusToString(StepStatus status) {
    switch (status) {
    case StepStatus::waiting:
        return "waiting";
    case StepStatus::running:
        return "running";
    case StepStatus::fixing:
        return "fixing";
    case StepStatus::done:
        return "done";
    case StepStatus::skipped:
        return "skipped";
    }
    return "waiting";
}

StepStatus stepStatusFromString(const juce::String& name, bool& known) {
    known = true;
    if (name == "running")
        return StepStatus::running;
    if (name == "fixing")
        return StepStatus::fixing;
    if (name == "done")
        return StepStatus::done;
    if (name == "skipped")
        return StepStatus::skipped;
    known = name == "waiting";
    return StepStatus::waiting;
}

int SongPlan::totalBars() const {
    int total = 0;
    for (const auto& section : sections)
        total += section.bars;
    return total;
}

double SongPlan::seconds() const { return bpm > 0 ? (double)totalBars() * 4.0 * 60.0 / (double)bpm : 0.0; }

juce::String SongPlan::durationText() const {
    const int whole = (int)std::lround(seconds());
    return juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
}

bool parseSongPlan(const juce::var& planVar, SongPlan& out) {
    if (!planVar.isObject())
        return false;
    SongPlan plan;
    plan.title = stringOf(planVar, "title");
    plan.key = stringOf(planVar, "key");
    plan.bpm = intOf(planVar, "bpm");
    plan.special = stringOf(planVar, "special");
    if (auto* sections = planVar.getProperty("sections", {}).getArray())
        for (const auto& s : *sections)
            plan.sections.push_back({stringOf(s, "name"), intOf(s, "bars"), juce::jlimit(1, 5, intOf(s, "energy", 1)),
                                     stringOf(s, "chords"), stringOf(s, "description")});
    if (auto* tracks = planVar.getProperty("tracks", {}).getArray())
        for (const auto& t : *tracks) {
            PlanTrack track{
                stringOf(t, "name"), stringOf(t, "role"), stringOf(t, "instrument"), stringOf(t, "sound"), {}};
            if (auto* names = t.getProperty("sections", {}).getArray())
                for (const auto& n : *names)
                    track.sections.add(n.toString());
            plan.tracks.push_back(std::move(track));
        }
    if (auto* lanes = planVar.getProperty("lanes", {}).getArray())
        plan.laneCount = lanes->size();
    plan.raw = planVar;
    if (plan.sections.empty() || plan.tracks.empty())
        return false;
    out = std::move(plan);
    return true;
}

bool parsePlanReply(const juce::String& dataJson, PlanReply& out, juce::String& error) {
    const juce::var data = juce::JSON::parse(dataJson);
    if (!data.isObject()) {
        error = "The plan was not readable.";
        return false;
    }
    PlanReply reply;
    if (!parseSongPlan(data.getProperty("plan", {}), reply.plan)) {
        error = "The plan had no sections or tracks.";
        return false;
    }
    reply.token = stringOf(data, "token");
    if (reply.token.isEmpty()) {
        error = "The plan came without a way to build it.";
        return false;
    }
    const auto price = data.getProperty("price", {});
    reply.units = juce::jmax(1, intOf(price, "units", 1));
    reply.estimatedUsd = (double)price.getProperty("estimatedUsd", 0.0);
    if (auto* steps = data.getProperty("steps", {}).getArray())
        for (const auto& s : *steps)
            reply.steps.push_back({stringOf(s, "id"), stepKindFromString(stringOf(s, "kind")), stringOf(s, "label")});
    out = std::move(reply);
    return true;
}

juce::String planMetaText(const SongPlan& plan) {
    juce::StringArray parts;
    parts.add(plan.durationText());
    if (plan.bpm > 0)
        parts.add(juce::String(plan.bpm) + " BPM");
    const int n = (int)plan.sections.size();
    parts.add(juce::String(n) + (n == 1 ? " section" : " sections"));
    return parts.joinIntoString(juce::String::fromUTF8(" \xC2\xB7 "));
}

std::optional<StreamEvent> parseStreamLine(const juce::String& line) {
    const auto trimmed = line.trim();
    if (trimmed.isEmpty())
        return std::nullopt;
    const juce::var json = juce::JSON::parse(trimmed);
    if (!json.isObject())
        return std::nullopt;
    const auto type = stringOf(json, "type");
    StreamEvent event;
    event.raw = json;
    if (type == "ping") {
        event.type = EventType::ping;
    } else if (type == "steps") {
        event.type = EventType::steps;
        auto* steps = json.getProperty("steps", {}).getArray();
        if (steps == nullptr)
            return std::nullopt;
        for (const auto& s : *steps)
            event.steps.push_back({stringOf(s, "id"), stepKindFromString(stringOf(s, "kind")), stringOf(s, "label")});
    } else if (type == "step") {
        event.type = EventType::step;
        event.stepId = stringOf(json, "id");
        bool known = false;
        event.status = stepStatusFromString(stringOf(json, "status"), known);
        if (event.stepId.isEmpty() || !known)
            return std::nullopt;
        event.ms = intOf(json, "ms", -1);
        event.stepSummary = stringOf(json, "summary");
    } else if (type == "done") {
        event.type = EventType::done;
        event.envelope = json.getProperty("envelope", {});
        if (!event.envelope.isObject())
            return std::nullopt;
        if (auto* notes = json.getProperty("notes", {}).getArray())
            for (const auto& n : *notes)
                event.notes.add(n.toString());
        const auto summary = json.getProperty("summary", {});
        event.tracks = intOf(summary, "tracks");
        event.sections = intOf(summary, "sections");
        event.bars = intOf(summary, "bars");
        event.seconds = (double)summary.getProperty("seconds", 0.0);
    } else if (type == "error") {
        event.type = EventType::error;
        event.message = stringOf(json, "message");
        event.refunded = (bool)json.getProperty("refunded", false);
    } else {
        return std::nullopt;
    }
    return event;
}

void LineSplitter::push(const char* data, std::size_t size, const LineHandler& onLine) {
    pending.append(data, size);
    std::size_t start = 0;
    for (;;) {
        const auto newline = pending.find('\n', start);
        if (newline == std::string::npos)
            break;
        std::size_t end = newline;
        if (end > start && pending[end - 1] == '\r')
            --end;
        if (end > start && onLine)
            onLine(juce::String::fromUTF8(pending.data() + start, (int)(end - start)));
        start = newline + 1;
    }
    pending.erase(0, start);
}

void LineSplitter::finish(const LineHandler& onLine) {
    if (!pending.empty() && onLine)
        onLine(juce::String::fromUTF8(pending.data(), (int)pending.size()));
    pending.clear();
}

// -- BuildModel ----------------------------------------------------------------------------------------------------

bool BuildModel::apply(const StreamEvent& event) {
    switch (event.type) {
    case EventType::steps:
        stepStates.clear();
        for (const auto& info : event.steps)
            stepStates.push_back({info, StepStatus::waiting, -1, {}});
        return true;
    case EventType::step: {
        auto it = std::find_if(stepStates.begin(), stepStates.end(),
                               [&](const StepState& s) { return s.info.id == event.stepId; });
        if (it == stepStates.end())
            return false;
        // A step never goes back: a late `running` after `done` (reordered lines) is ignored.
        const bool over = it->status == StepStatus::done || it->status == StepStatus::skipped;
        if (over && event.status != StepStatus::done && event.status != StepStatus::skipped)
            return false;
        it->status = event.status;
        if (event.ms >= 0)
            it->ms = event.ms;
        if (event.stepSummary.isNotEmpty())
            it->summary = event.stepSummary;
        return true;
    }
    case EventType::done:
        markDone(event);
        return true;
    case EventType::error:
        markFailed(event.message, event.refunded);
        return true;
    case EventType::ping:
        return false;
    }
    return false;
}

int BuildModel::finishedCount() const {
    return (int)std::count_if(stepStates.begin(), stepStates.end(), [](const StepState& s) {
        return s.status == StepStatus::done || s.status == StepStatus::skipped;
    });
}

int BuildModel::currentIndex() const {
    for (int i = 0; i < total(); ++i)
        if (stepStates[(size_t)i].status == StepStatus::fixing)
            return i;
    for (int i = 0; i < total(); ++i)
        if (stepStates[(size_t)i].status == StepStatus::running)
            return i;
    return -1;
}

int BuildModel::currentNumber() const { return juce::jmin(total(), finishedCount() + 1); }

float BuildModel::progress() const {
    if (total() == 0)
        return 0.0f;
    if (isDone())
        return 1.0f;
    return (float)finishedCount() / (float)total();
}

bool BuildModel::hasSkipped() const {
    return std::any_of(stepStates.begin(), stepStates.end(),
                       [](const StepState& s) { return s.status == StepStatus::skipped; });
}

void BuildModel::markFailed(const juce::String& message, bool refundedUses) {
    finished = true;
    failed = true;
    failure = message;
    refunded = refundedUses;
    for (auto& step : stepStates)
        if (step.status == StepStatus::running || step.status == StepStatus::fixing)
            step.status = StepStatus::waiting;
}

void BuildModel::markDone(const StreamEvent& doneEventIn) {
    finished = true;
    failed = false;
    finalEvent = doneEventIn;
    for (auto& step : stepStates)
        if (step.status == StepStatus::running || step.status == StepStatus::fixing)
            step.status = StepStatus::done;
}

juce::String BuildModel::resultLine() const {
    juce::StringArray parts;
    parts.add(juce::String(total()) + (total() == 1 ? " step" : " steps"));
    if (finalEvent.sections > 0)
        parts.add(juce::String(finalEvent.sections) + (finalEvent.sections == 1 ? " section" : " sections"));
    if (finalEvent.seconds > 0) {
        const int whole = (int)std::lround(finalEvent.seconds);
        parts.add(juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2));
    }
    auto line = parts.joinIntoString(", ");
    const int skipped = (int)std::count_if(stepStates.begin(), stepStates.end(),
                                           [](const StepState& s) { return s.status == StepStatus::skipped; });
    if (skipped > 0)
        line << " (" << skipped << (skipped == 1 ? " detail" : " details") << " left out)";
    return line;
}

} // namespace synth::songsteps
