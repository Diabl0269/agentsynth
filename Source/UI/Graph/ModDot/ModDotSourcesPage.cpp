#include "ModDotSourcesPage.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ModDotController.h"
#include "ModDotPalette.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
SplitButtonHalfSpec addSourceHalf() {
    return {ModDotGlyph::List, "Add source", "Add source", "Add source, list open", "Add source from a list"};
}
SplitButtonHalfSpec pickOnCanvasHalf() {
    return {ModDotGlyph::Crosshair, "Pick on canvas", "Pick on canvas", "Pick on canvas, on",
            "Pick a source on the canvas. Esc stops"};
}
} // namespace

ModDotSourcesPage::ModDotSourcesPage(GraphEditor& editor, ModDotController& controller,
                                     juce::AudioProcessorGraph::NodeID card, int destChannel, KnobModTarget target)
    : editor_(editor)
    , controller_(controller)
    , card_(card)
    , destChannel_(destChannel)
    , target_(std::move(target))
    , split_(addSourceHalf(), pickOnCanvasHalf(), "Add a source")
    , updater_(this) {
    setTitle(target_.paramName + " modulation sources");
    addAndMakeVisible(split_);
    split_.leftHalf().onClick = [this] {
        if (onAddSourceRequested)
            onAddSourceRequested();
    };
    split_.rightHalf().onClick = [this] {
        if (onPickOnCanvasRequested)
            onPickOnCanvasRequested();
    };
    for (const auto& source : knobModSources(editor_, card_, destChannel_, true)) {
        Entry entry;
        entry.row = std::make_unique<ModDotSourceRow>(source, target_.paramName);
        entry.current = entry.to = (float)ModDotSourceRow::kHeight;
        wireRow(*entry.row);
        addAndMakeVisible(*entry.row);
        entries_.push_back(std::move(entry));
    }
    const auto chosen = controller_.chosenAttenuverter(card_, destChannel_);
    for (auto& e : entries_)
        e.row->setSelected(e.row->attenuverterId() == chosen);
    restoreSerialSeen_ = restoreSerial();
    layoutRows();
    setSize(kWidth, preferredHeight());
}

ModDotSourcesPage::~ModDotSourcesPage() { anim_.stop(updater_); }

void ModDotSourcesPage::wireRow(ModDotSourceRow& row) {
    row.onSelect = [this](ModDotSourceRow& r) { select(r.attenuverterId()); };
    row.onGestureBegin = [this](ModDotSourceRow&) { editor_.beginModAmountGesture(); };
    row.onAmountDragged = [this](ModDotSourceRow& r, float amount) { applyAmount(r, amount); };
    row.onGestureEnd = [this](ModDotSourceRow&) { editor_.commitModAmountGesture(); };
    row.onAmountTyped = [this](ModDotSourceRow& r, float amount) {
        editor_.beginModAmountGesture();
        applyAmount(r, amount);
        editor_.commitModAmountGesture();
        sync();
    };
    row.onShowInTimeline = [this](ModDotSourceRow& r) {
        controller_.revealSource(card_, destChannel_, r.attenuverterId());
    };
    row.onRemove = [this](ModDotSourceRow& r) {
        controller_.removeSource(card_, destChannel_, r.attenuverterId());
        sync();
        focusEntry();
    };
    row.onNavigate = [this](ModDotSourceRow& r, int step) { navigate(&r, step); };
}

void ModDotSourcesPage::applyAmount(ModDotSourceRow& row, float amount) {
    const float current = attenuverterAmount(editor_.getAudioEngine().getGraph(), row.attenuverterId(), amount);
    editor_.adjustModAmount(row.attenuverterId(), amount - current);
}

void ModDotSourcesPage::setRemoveHighlighted(bool on) {
    removeHighlighted_ = on;
    for (auto& e : entries_)
        e.row->setRemoveHighlighted(on && !e.leaving);
}

void ModDotSourcesPage::select(juce::AudioProcessorGraph::NodeID attenuverterId) {
    controller_.setLastChosen(card_, destChannel_, attenuverterId);
    for (auto& e : entries_)
        e.row->setSelected(!e.leaving && e.row->attenuverterId() == attenuverterId);
}

// True when `sources` are exactly the rows that are not on their way out.
bool ModDotSourcesPage::followsSameSources(const std::vector<KnobModSource>& sources) const {
    const auto isShown = [this](const KnobModSource& s) {
        return std::any_of(entries_.begin(), entries_.end(),
                           [&s](const Entry& e) { return !e.leaving && e.row->attenuverterId() == s.attenuverterId; });
    };
    const auto shown = std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.leaving; });
    return static_cast<size_t>(shown) == sources.size() && std::all_of(sources.begin(), sources.end(), isShown);
}

// The number of undo/redo steps the document has taken: a row that appears after it moved came back from one.
int ModDotSourcesPage::restoreSerial() const {
    const auto* undo = editor_.getUndoManager();
    return undo != nullptr ? undo->getRestoreSerial() : 0;
}

void ModDotSourcesPage::sync(bool fresh) {
    const auto sources = knobModSources(editor_, card_, destChannel_, fresh);
    const int serial = restoreSerial();
    const bool restoredByUndo = serial != restoreSerialSeen_;
    restoreSerialSeen_ = serial;
    // The same sources as the motion in flight was started for: only the amounts follow, the motion plays on.
    if (motionActive_ && followsSameSources(sources)) {
        for (auto& e : entries_)
            if (!e.leaving)
                e.row->update(*std::find_if(sources.begin(), sources.end(), [&e](const KnobModSource& s) {
                    return s.attenuverterId == e.row->attenuverterId();
                }));
        return;
    }
    landMotion(); // a different change lands the one playing first
    for (auto& e : entries_) {
        const auto it = std::find_if(sources.begin(), sources.end(), [&e](const KnobModSource& s) {
            return s.attenuverterId == e.row->attenuverterId();
        });
        e.leaving = it == sources.end();
        if (!e.leaving)
            e.row->update(*it);
    }
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        const bool known = std::any_of(entries_.begin(), entries_.end(), [&source](const Entry& e) {
            return e.row->attenuverterId() == source.attenuverterId;
        });
        if (known)
            continue;
        Entry entry;
        entry.row = std::make_unique<ModDotSourceRow>(source, target_.paramName);
        entry.restored = restoredByUndo;
        wireRow(*entry.row);
        entry.row->setRemoveHighlighted(removeHighlighted_);
        addAndMakeVisible(*entry.row);
        // In the graph's own order: after the row of the closest source before it that is shown.
        auto at = entries_.begin();
        for (size_t before = i; before-- > 0;) {
            const auto it = std::find_if(entries_.begin(), entries_.end(), [&sources, before](const Entry& e) {
                return e.row->attenuverterId() == sources[before].attenuverterId;
            });
            if (it != entries_.end()) {
                at = it + 1;
                break;
            }
        }
        entries_.insert(at, std::move(entry));
    }
    const auto chosen = controller_.chosenAttenuverter(card_, destChannel_);
    for (auto& e : entries_)
        e.row->setSelected(!e.leaving && e.row->attenuverterId() == chosen);
    animateLayout();
}

// What the change asks for. A removed row (by the panel, the timeline, a cable cut or an undo) shrinks away and then
// the rows below close the gap; a row an undo brings back is made room for, grows and gets a fading outline: the shared
// ExitEnterTimeline, one phase after another. A source the user just added only grows in. Everything lands at once when
// the panel is not on screen.
void ModDotSourcesPage::animateLayout() {
    const bool anyLeaving = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving; });
    const bool anyRestored = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.restored; });
    if (anyLeaving || anyRestored) {
        startRemovalMotion();
        return;
    }
    growNewRows();
}

// A row the user added grows from nothing to full (160 ms); the rows below follow because they are placed from the
// heights above them.
void ModDotSourcesPage::growNewRows() {
    bool moving = false;
    for (auto& e : entries_) {
        e.from = e.current;
        e.to = (float)ModDotSourceRow::kHeight;
        moving = moving || e.from != e.to;
    }
    if (!moving) {
        layoutRows();
        return;
    }
    if (!modDotMotionAllowed(*this)) {
        for (auto& e : entries_)
            e.current = e.to;
        layoutRows();
        return;
    }
    anim_.start(
        updater_, kGrowMs, easeOutCubic,
        [this](float t) {
            for (auto& e : entries_)
                e.current = e.from + (e.to - e.from) * t;
            layoutRows();
        },
        [this] {
            for (auto& e : entries_)
                e.current = e.to;
            layoutRows();
        });
}

void ModDotSourcesPage::startRemovalMotion() {
    const float full = (float)ModDotSourceRow::kHeight;
    for (auto& e : entries_) {
        e.from = e.leaving ? full : (e.restored ? 0.0f : full);
        e.to = e.leaving ? 0.0f : full;
        e.current = e.from;
        if (e.leaving) {
            e.row->setEnabled(false); // a row on its way out is not a control any more
            e.row->setInterceptsMouseClicks(false, false);
        }
    }
    if (!(isShowing() || forceAnimate_)) {
        landMotion();
        return;
    }
    reducedMotion_ = prefersReducedMotion();
    timeline_ = {};
    timeline_.hasExit = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving; });
    timeline_.hasEnter = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.restored; });
    timeline_.hasGap = true;
    motionActive_ = true;
    applyTimelineAtMs(0.0);
    const double total = timeline_.totalMs();
    anim_.start(
        updater_, total, [](float t) { return t; }, [this, total](float t) { applyTimelineAtMs((double)t * total); },
        [this] { landMotion(); });
}

// Where the motion stands `elapsedMs` after it began: the rows leaving shrink (exit) and then their slots close (gap);
// the rows restored open their slots (gap), grow, and fade an outline.
void ModDotSourcesPage::applyTimelineAtMs(double elapsedMs) {
    if (!motionActive_)
        return;
    using synth::ui::ExitEnterTimeline;
    const auto frame = timeline_.at(elapsedMs);
    for (auto& e : entries_) {
        if (!e.leaving && !e.restored)
            continue;
        e.current = e.from + (e.to - e.from) * frame.gap;
        if (e.leaving) {
            e.scale = ExitEnterTimeline::ghostScale(frame.exit, true, reducedMotion_);
            e.alpha = ExitEnterTimeline::ghostAlpha(frame.exit, true, reducedMotion_);
        } else {
            e.scale = ExitEnterTimeline::ghostScale(frame.grow, false, reducedMotion_);
            e.alpha = ExitEnterTimeline::ghostAlpha(frame.grow, false, reducedMotion_);
            e.outline = frame.grow >= 1.0f ? 1.0f - frame.outline : 0.0f;
        }
    }
    layoutRows();
    repaint();
}

// Every row at its final place and look: the rows that left are gone, the ones that came back are ordinary rows.
void ModDotSourcesPage::landMotion() {
    const bool wasActive = motionActive_ || std::any_of(entries_.begin(), entries_.end(),
                                                        [](const Entry& e) { return e.leaving || e.restored; });
    if (!wasActive)
        return;
    motionActive_ = false;
    for (auto& e : entries_) {
        e.current = e.leaving ? 0.0f : (float)ModDotSourceRow::kHeight;
        e.restored = false;
        e.scale = e.alpha = 1.0f;
        e.outline = 0.0f;
    }
    finishLeaving();
    layoutRows();
    repaint();
}

void ModDotSourcesPage::finishLeaving() {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving; }),
                   entries_.end());
}

void ModDotSourcesPage::layoutRows() {
    int y = kTitleHeight;
    for (auto& e : entries_) {
        const int h = juce::roundToInt(e.current);
        e.row->setBounds(0, y, getWidth(), h);
        e.row->setVisible(h > 0 && e.scale > 0.0f && e.alpha > 0.0f);
        e.row->setAlpha(e.alpha);
        if (e.scale < 1.0f) {
            const auto c = e.row->getBounds().toFloat().getCentre();
            e.row->setTransform(juce::AffineTransform::scale(e.scale, e.scale, c.x, c.y));
        } else {
            e.row->setTransform({});
        }
        y += h;
    }
    dividerY_ = y + 3;
    split_.setBounds(6, dividerY_ + 4, getWidth() - 12, SplitButton::kHeight);
    heightChanged();
}

ModDotSourceRow* ModDotSourcesPage::animatingRowFor(juce::AudioProcessorGraph::NodeID attenuverterId) const {
    for (const auto& e : entries_)
        if (e.row->attenuverterId() == attenuverterId)
            return e.row.get();
    return nullptr;
}

int ModDotSourcesPage::slotHeightFor(juce::AudioProcessorGraph::NodeID attenuverterId) const {
    for (const auto& e : entries_)
        if (e.row->attenuverterId() == attenuverterId)
            return juce::roundToInt(e.current);
    return 0;
}

float ModDotSourcesPage::outlineAlphaFor(juce::AudioProcessorGraph::NodeID attenuverterId) const {
    for (const auto& e : entries_)
        if (e.row->attenuverterId() == attenuverterId)
            return e.outline;
    return 0.0f;
}

// The 1 px accent outline around a row an undo brought back, fading once it has grown.
void ModDotSourcesPage::paintOverChildren(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    for (const auto& e : entries_) {
        if (e.outline <= 0.0f)
            continue;
        g.setColour(p.accent.withAlpha(e.outline));
        g.drawRoundedRectangle(e.row->getBounds().toFloat().reduced(0.5f), p.radius, 1.0f);
    }
}

int ModDotSourcesPage::preferredHeight() const { return dividerY_ + 4 + SplitButton::kHeight + 6; }

void ModDotSourcesPage::resized() { layoutRows(); }

void ModDotSourcesPage::paint(juce::Graphics& g) {
    const auto p = modDotPaletteFor(*this);
    g.setColour(p.text);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.drawText(titleText(), juce::Rectangle<int>(0, 0, getWidth(), kTitleHeight).reduced(12, 0),
               juce::Justification::centredLeft, true);
    g.setColour(p.border);
    g.fillRect(juce::Rectangle<int>(8, dividerY_, getWidth() - 16, 1));
}

juce::String ModDotSourcesPage::titleText() const {
    return target_.paramName + juce::String::fromUTF8(" \xC2\xB7 modulation");
}

void ModDotSourcesPage::focusEntry() {
    for (auto& e : entries_)
        if (!e.leaving && e.row->isSelected()) {
            e.row->bar().grabKeyboardFocus();
            return;
        }
    for (auto& e : entries_)
        if (!e.leaving) {
            e.row->bar().grabKeyboardFocus();
            return;
        }
    split_.leftHalf().grabKeyboardFocus();
}

// Up/Down walk the rows (each row's bar) and end on the split button's list half.
void ModDotSourcesPage::navigate(juce::Component* from, int step) {
    struct Stop {
        juce::Component* control;
        juce::Component* container;
    };
    std::vector<Stop> stops;
    for (auto& e : entries_)
        if (!e.leaving)
            stops.push_back({&e.row->bar(), e.row.get()});
    stops.push_back({&split_.leftHalf(), &split_});
    int index = (int)stops.size() - 1;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (from != nullptr && (stops[(size_t)i].container == from || stops[(size_t)i].container->isParentOf(from)))
            index = i;
    index = juce::jlimit(0, (int)stops.size() - 1, index + step);
    stops[(size_t)index].control->grabKeyboardFocus();
}

bool ModDotSourcesPage::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        navigate(juce::Component::getCurrentlyFocusedComponent(), key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

int ModDotSourcesPage::rowCount() const {
    int n = 0;
    for (const auto& e : entries_)
        n += e.leaving ? 0 : 1;
    return n;
}

ModDotSourceRow* ModDotSourcesPage::rowAt(int index) const {
    int n = 0;
    for (const auto& e : entries_)
        if (!e.leaving && n++ == index)
            return e.row.get();
    return nullptr;
}

ModDotSourceRow* ModDotSourcesPage::rowFor(juce::AudioProcessorGraph::NodeID attenuverterId) const {
    for (const auto& e : entries_)
        if (!e.leaving && e.row->attenuverterId() == attenuverterId)
            return e.row.get();
    return nullptr;
}

} // namespace synth::ui
