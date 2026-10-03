#include "ModDotSourcesPage.h"

#include "AudioEngine/AudioEngine.h"
#include "ModDotController.h"
#include "ModDotPalette.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/FocusRing.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

class ModDotSourcesPage::AddButton final : public juce::Button {
public:
    AddButton()
        : juce::Button("Add source")
        , hover_(*this) {
        setTitle("Add source");
        setTooltip("Add source");
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
            triggerClick();
            return true;
        }
        return false;
    }
    void mouseEnter(const juce::MouseEvent& e) override {
        juce::Button::mouseEnter(e);
        hover_.setHovered(true);
    }
    void mouseExit(const juce::MouseEvent& e) override {
        juce::Button::mouseExit(e);
        hover_.setHovered(false);
    }
    void paintButton(juce::Graphics& g, bool, bool) override {
        const auto p = modDotPaletteFor(*this);
        const auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);
        g.setColour(p.hover.withAlpha(hover_.value()));
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(p.accent);
        g.setFont(juce::Font(juce::FontOptions(12.5f)));
        g.drawText("+ Add source", getLocalBounds().reduced(10, 0), juce::Justification::centredLeft);
        paintFocusRing(g, area, *this, 6.0f);
    }

private:
    ModDotHoverFade hover_;
};

ModDotSourcesPage::ModDotSourcesPage(GraphEditor& editor, ModDotController& controller,
                                     juce::AudioProcessorGraph::NodeID card, int destChannel, KnobModTarget target)
    : editor_(editor)
    , controller_(controller)
    , card_(card)
    , destChannel_(destChannel)
    , target_(std::move(target))
    , addButton_(std::make_unique<AddButton>())
    , updater_(this) {
    setTitle(target_.paramName + " modulation sources");
    addAndMakeVisible(*addButton_);
    addButton_->onClick = [this] {
        if (onAddSourceRequested)
            onAddSourceRequested();
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

void ModDotSourcesPage::select(juce::AudioProcessorGraph::NodeID attenuverterId) {
    controller_.setLastChosen(card_, destChannel_, attenuverterId);
    for (auto& e : entries_)
        e.row->setSelected(!e.leaving && e.row->attenuverterId() == attenuverterId);
}

void ModDotSourcesPage::sync(bool fresh) {
    const auto sources = knobModSources(editor_, card_, destChannel_, fresh);
    for (auto& e : entries_) {
        const auto it = std::find_if(sources.begin(), sources.end(), [&e](const KnobModSource& s) {
            return s.attenuverterId == e.row->attenuverterId();
        });
        e.leaving = it == sources.end();
        if (!e.leaving)
            e.row->update(*it);
    }
    for (const auto& source : sources) {
        const bool known = std::any_of(entries_.begin(), entries_.end(), [&source](const Entry& e) {
            return e.row->attenuverterId() == source.attenuverterId;
        });
        if (known)
            continue;
        Entry entry;
        entry.row = std::make_unique<ModDotSourceRow>(source, target_.paramName);
        wireRow(*entry.row);
        addAndMakeVisible(*entry.row);
        entries_.push_back(std::move(entry));
    }
    const auto chosen = controller_.chosenAttenuverter(card_, destChannel_);
    for (auto& e : entries_)
        e.row->setSelected(!e.leaving && e.row->attenuverterId() == chosen);
    animateLayout();
}

// Each row's height runs from where it is to full (a new row starts at zero) or to nothing (a removed row); the
// rows below follow because they are placed from the heights above them. Landing at once when not on screen.
void ModDotSourcesPage::animateLayout() {
    bool moving = false;
    for (auto& e : entries_) {
        e.from = e.current;
        e.to = e.leaving ? 0.0f : (float)ModDotSourceRow::kHeight;
        moving = moving || e.from != e.to;
    }
    if (!moving) {
        layoutRows();
        return;
    }
    if (!modDotMotionAllowed(*this)) {
        for (auto& e : entries_)
            e.current = e.to;
        finishLeaving();
        layoutRows();
        return;
    }
    bool anyLeaving = std::any_of(entries_.begin(), entries_.end(), [](const Entry& e) { return e.leaving; });
    anim_.start(
        updater_, anyLeaving ? kShrinkMs : kGrowMs, [](float t) { return t; },
        [this](float t) {
            for (auto& e : entries_) {
                const float eased = e.leaving ? easeInCubic(t) : easeOutCubic(t);
                e.current = e.from + (e.to - e.from) * eased;
            }
            layoutRows();
        },
        [this] {
            for (auto& e : entries_)
                e.current = e.to;
            finishLeaving();
            layoutRows();
        });
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
        e.row->setVisible(h > 0);
        y += h;
    }
    dividerY_ = y + 3;
    addButton_->setBounds(0, dividerY_ + 4, getWidth(), kAddHeight - 6);
    heightChanged();
}

int ModDotSourcesPage::preferredHeight() const { return dividerY_ + 4 + kAddHeight - 6 + 6; }

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
    addButton_->grabKeyboardFocus();
}

// Up/Down walk the rows (each row's bar) and end on "+ Add source".
void ModDotSourcesPage::navigate(juce::Component* from, int step) {
    struct Stop {
        juce::Component* control;
        juce::Component* container;
    };
    std::vector<Stop> stops;
    for (auto& e : entries_)
        if (!e.leaving)
            stops.push_back({&e.row->bar(), e.row.get()});
    stops.push_back({addButton_.get(), addButton_.get()});
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

juce::Button& ModDotSourcesPage::addButton() noexcept { return *addButton_; }

} // namespace synth::ui
