// Concern: the Audio tab's Inputs section -- the table of the interface's inputs, their pairing, names, levels and Use
// ticks.
#include "UI/Settings/AudioSettingsTab/AudioInputsTable.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {
constexpr int kHeadHeight = 22;
constexpr int kJacksWidth = 64;
constexpr int kPairWidth = 40;
constexpr int kKindWidth = 52;
constexpr int kLevelWidth = 110;
constexpr int kUseWidth = 40;
constexpr int kBarWidth = 3;

struct Columns {
    juce::Rectangle<int> jacks, pair, name, kind, level, use;
};

Columns columnsFor(int rowWidth, int y, int height) {
    auto area = juce::Rectangle<int>(0, y, rowWidth, height).reduced(AudioSettingsSection::kPad, 0);
    Columns c;
    c.jacks = area.removeFromLeft(kJacksWidth);
    c.pair = area.removeFromLeft(kPairWidth);
    c.use = area.removeFromRight(kUseWidth);
    c.level = area.removeFromRight(kLevelWidth);
    c.kind = area.removeFromRight(kKindWidth);
    c.name = area.reduced(4, 0);
    return c;
}

const char* kCaption = "Jacks come in neighbouring pairs (1-2, 3-4). The chain button joins a pair into one stereo "
                       "input or splits it into two mono ones. Mono for more instruments, stereo for fewer. A name "
                       "is optional; with none, tracks show 'In 1'.";
} // namespace

AudioInputsTable::Row::Row(const juce::String& jacksText, int first, int count, bool isPaired)
    : firstJack(first)
    , numJacks(count)
    , paired(isPaired)
    , level(std::make_unique<AudioInputLevelCell>(jacksText))
    , use(jacksText) {
    jacks.setText(jacksText, juce::dontSendNotification);
    jacks.setJustificationType(juce::Justification::centredLeft);
    kind.setText(count > 1 ? "Stereo" : "Mono", juce::dontSendNotification);
    kind.setJustificationType(juce::Justification::centredLeft);
    name.setTextToShowWhenEmpty(jacksText, juce::Colours::grey); // recoloured in lookAndFeelChanged
    name.setTitle("Name for " + jacksText);
    name.setTooltip("Name for " + jacksText + " (optional; tracks pick this input by its name)");
    name.setInputRestrictions(40);
    name.setSelectAllWhenFocused(true);
    level->setChannelCount(count);
    use.setButtonText({});
    use.setTitle("Use " + jacksText);
    use.setTooltip("Use " + jacksText + ": turn this input on for recording and monitoring");
    for (auto* child : std::initializer_list<juce::Component*>{&jacks, &name, &kind, level.get(), &use})
        addAndMakeVisible(*child);
    // Tab visits a row left to right: name, level, use.
    name.setExplicitFocusOrder(1);
    level->setExplicitFocusOrder(2);
    use.setExplicitFocusOrder(3);
    setInterceptsMouseClicks(false, true);
}

void AudioInputsTable::Row::resized() {
    const auto c = columnsFor(getWidth(), 0, getHeight());
    jacks.setBounds(c.jacks);
    name.setBounds(c.name.reduced(0, 3));
    kind.setBounds(c.kind);
    level->setBounds(c.level);
    use.setBounds(c.use.withSizeKeepingCentre(24, 24));
}

// A cyan bar down the row's edge marks a pair that is joined.
void AudioInputsTable::Row::paint(juce::Graphics& g) {
    if (!paired)
        return;
    g.setColour(synth::theme::themeOf(*this).colors.accent);
    g.fillRoundedRectangle(juce::Rectangle<float>(1.0f, 3.0f, (float)kBarWidth, (float)getHeight() - 6.0f), 1.5f);
}

AudioInputsTable::AudioInputsTable(AudioSettingsSource& source)
    : source_(source) {
    none_.setText("No audio input is open. Pick an interface with inputs in Device.", juce::dontSendNotification);
    addChildComponent(none_);
    caption_.setText(kCaption, juce::dontSendNotification);
    caption_.setFont(juce::Font(juce::FontOptions(11.5f)));
    caption_.setMinimumHorizontalScale(1.0f);
    caption_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(caption_);
    refresh();
}

AudioInputsTable::~AudioInputsTable() { stopTimer(); }

juce::String AudioInputsTable::jacksText(int first, int count) {
    return count > 1 ? "In " + juce::String(first + 1) + " + " + juce::String(first + 2)
                     : "In " + juce::String(first + 1);
}

juce::String AudioInputsTable::summary() const {
    if (info_.deviceName.isEmpty())
        return "No audio device";
    const int jacks = juce::jmin(info_.numInputs, synth::AudioInputLayoutStore::kMaxJacks);
    return info_.deviceName + juce::String::fromUTF8(" \xC2\xB7 ") + juce::String(jacks) +
           (jacks == 1 ? " jack" : " jacks");
}

int AudioInputsTable::preferredHeight(int width) const {
    const int captionHeight = wrappedHeight(kCaption, caption_.getFont(), width - 2 * kPad);
    const int body = rows_.empty() ? kRowHeight : kHeadHeight + (int)rows_.size() * kRowHeight;
    return kPad + body + 6 + captionHeight + kPad;
}

int AudioInputsTable::rowTop(int row) const { return kPad + kHeadHeight + row * kRowHeight; }

juce::Rectangle<int> AudioInputsTable::getRowBoundsForTest(int row) const { return rows_[(size_t)row]->getBounds(); }

void AudioInputsTable::pollMeters() {
    for (auto& row : rows_)
        row->level->setLevels(source_.readInputPeak(row->firstJack),
                              row->numJacks > 1 ? source_.readInputPeak(row->firstJack + 1) : 0.0f);
}

void AudioInputsTable::updateTimer() {
    if (isShowing() && !rows_.empty())
        startTimerHz(15);
    else
        stopTimer();
}

void AudioInputsTable::visibilityChanged() { updateTimer(); }
void AudioInputsTable::parentHierarchyChanged() { updateTimer(); }

void AudioInputsTable::refresh() {
    info_ = source_.currentInterface();
    const int jacks =
        info_.deviceName.isEmpty() ? 0 : juce::jmin(info_.numInputs, synth::AudioInputLayoutStore::kMaxJacks);
    std::vector<Spec> specs;
    for (const auto& slot : source_.layoutStore().slotsFor(info_.deviceType, info_.deviceName, jacks))
        specs.push_back({slot.firstJack, slot.numJacks});
    if (!built_ || specs != specs_ || (int)pairButtons_.size() != jacks / 2)
        rebuild(specs, jacks);
    updateInPlace(info_);
    none_.setVisible(rows_.empty());
    updateTimer();
}

void AudioInputsTable::rebuild(const std::vector<Spec>& specs, int numJacks) {
    // A fade still arriving is finished first, so no row is left half transparent behind a new one.
    if (arriveFade_ != nullptr) {
        arriveFade_->snapTo(true);
        arriveFade_.reset();
    }
    for (auto& row : rows_)
        leaving_.retire(std::move(row));
    rows_.clear();
    pairButtons_.clear();

    int order = 0;
    std::vector<juce::Component*> arriving;
    const auto addRow = [&](const Spec& spec) {
        auto row = std::make_unique<Row>(jacksText(spec.first, spec.count), spec.first, spec.count, spec.count > 1);
        row->setExplicitFocusOrder(++order);
        const int rowIndex = (int)rows_.size();
        row->name.onReturnKey = [this, rowIndex] { commitName(rowIndex); };
        row->name.onFocusLost = [this, rowIndex] { commitName(rowIndex); };
        row->use.onClick = [this, raw = row.get()] {
            source_.setInputsUsed(raw->firstJack, raw->numJacks, raw->use.getToggleState());
        };
        addAndMakeVisible(*row);
        if (onEditorCreated)
            onEditorCreated(row->name);
        arriving.push_back(row.get());
        rows_.push_back(std::move(row));
    };
    // Each pair's button comes before its rows in Tab order; the odd last jack has no pair.
    for (int pair = 0; pair < numJacks / 2; ++pair) {
        auto button = std::make_unique<synth::ui::IconButton>("pair" + juce::String(pair), synth::theme::Glyph::Link,
                                                              synth::ui::IconButton::Style::Framed);
        button->setWantsKeyboardFocus(true);
        button->setExplicitFocusOrder(++order);
        button->onClick = [this, pair] {
            auto& layout = source_.layoutStore();
            layout.setPaired(info_.deviceType, info_.deviceName, pair,
                             !layout.isPaired(info_.deviceType, info_.deviceName, pair));
        };
        addAndMakeVisible(*button);
        arriving.push_back(button.get());
        pairButtons_.push_back(std::move(button));
        for (const auto& spec : specs)
            if (spec.first / 2 == pair)
                addRow(spec);
    }
    for (const auto& spec : specs)
        if (spec.first / 2 >= numJacks / 2)
            addRow(spec);
    specs_ = specs;

    if (built_ && !arriving.empty()) {
        arriveFade_ = std::make_unique<synth::ui::FadeVisibility>(arriving);
        arriveFade_->snapTo(false);
        arriveFade_->setShown(true);
    }
    built_ = true;
    resized();
}

// Everything that can change without the rows changing: names, the Use ticks, the pair buttons' look.
void AudioInputsTable::updateInPlace(const AudioInterfaceInfo& info) {
    const auto& store = source_.layoutStore();
    for (auto& row : rows_) {
        if (!row->name.hasKeyboardFocus(false))
            row->name.setText(store.getName(info.deviceType, info.deviceName, row->firstJack),
                              juce::dontSendNotification);
        bool allOn = true;
        for (int jack = row->firstJack; jack < row->firstJack + row->numJacks; ++jack)
            allOn = allOn && (info.usedMask & (std::uint32_t{1} << jack)) != 0;
        row->use.setToggleState(allOn, juce::dontSendNotification);
        const auto colour = row->findColour(juce::Label::textColourId).withAlpha(0.6f);
        row->name.setTextToShowWhenEmpty(jacksText(row->firstJack, row->numJacks), colour);
    }
    for (size_t pair = 0; pair < pairButtons_.size(); ++pair) {
        auto& button = *pairButtons_[pair];
        const bool paired = store.isPaired(info.deviceType, info.deviceName, (int)pair);
        const int first = (int)pair * 2;
        button.setToggleState(paired, juce::dontSendNotification);
        const auto text =
            paired ? "Split " + jacksText(first, 2) + " into two mono inputs"
                   : "Pair " + jacksText(first, 1) + " and " + jacksText(first + 1, 1) + " as one stereo input";
        button.setTitle(text);
        button.setTooltip(text);
    }
    resized();
    repaint();
}

void AudioInputsTable::commitName(int row) {
    if (row < 0 || row >= (int)rows_.size())
        return;
    auto& r = *rows_[(size_t)row];
    source_.layoutStore().setName(info_.deviceType, info_.deviceName, r.firstJack, r.name.getText());
}

void AudioInputsTable::resized() {
    auto area = getLocalBounds();
    for (size_t i = 0; i < rows_.size(); ++i)
        rows_[i]->setBounds(0, rowTop((int)i), getWidth(), kRowHeight);
    for (size_t pair = 0; pair < pairButtons_.size(); ++pair) {
        // The button spans the rows of its pair.
        int top = -1, bottom = 0;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i]->firstJack / 2 == (int)pair) {
                top = top < 0 ? rowTop((int)i) : top;
                bottom = rowTop((int)i) + kRowHeight;
            }
        if (top < 0)
            continue;
        const auto c = columnsFor(getWidth(), top, bottom - top);
        pairButtons_[pair]->setBounds(c.pair.withSizeKeepingCentre(28, 28));
    }
    none_.setBounds(kPad, kPad, getWidth() - 2 * kPad, kRowHeight);
    const int captionHeight = wrappedHeight(kCaption, caption_.getFont(), getWidth() - 2 * kPad);
    const int bodyBottom = rows_.empty() ? kPad + kRowHeight : rowTop((int)rows_.size());
    caption_.setBounds(kPad, bodyBottom + 6, getWidth() - 2 * kPad, captionHeight);
}

void AudioInputsTable::paint(juce::Graphics& g) {
    if (rows_.empty())
        return;
    const auto& colours = synth::theme::themeOf(*this).colors;
    const auto c = columnsFor(getWidth(), kPad, kHeadHeight);
    g.setColour(colours.textMuted);
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    const std::pair<const char*, juce::Rectangle<int>> heads[] = {
        {"Jacks", c.jacks}, {"Pair", c.pair},   {"Name", c.name.withTrimmedLeft(4)},
        {"Kind", c.kind},   {"Level", c.level}, {"Use", c.use}};
    for (const auto& [text, bounds] : heads)
        g.drawText(text, bounds, juce::Justification::centredLeft);
    g.setColour(colours.border);
    g.fillRect(kPad, kPad + kHeadHeight - 1, getWidth() - 2 * kPad, 1);
}

synth::ui::IconButton* AudioInputsTable::getPairButtonForTest(int pair) {
    return pair >= 0 && pair < (int)pairButtons_.size() ? pairButtons_[(size_t)pair].get() : nullptr;
}
juce::TextEditor* AudioInputsTable::getNameEditorForTest(int row) {
    return row >= 0 && row < (int)rows_.size() ? &rows_[(size_t)row]->name : nullptr;
}
juce::ToggleButton* AudioInputsTable::getUseTickForTest(int row) {
    return row >= 0 && row < (int)rows_.size() ? &rows_[(size_t)row]->use : nullptr;
}
AudioInputLevelCell* AudioInputsTable::getLevelCellForTest(int row) {
    return row >= 0 && row < (int)rows_.size() ? rows_[(size_t)row]->level.get() : nullptr;
}
juce::String AudioInputsTable::getJacksTextForTest(int row) const { return rows_[(size_t)row]->jacks.getText(); }
juce::String AudioInputsTable::getKindTextForTest(int row) const { return rows_[(size_t)row]->kind.getText(); }
