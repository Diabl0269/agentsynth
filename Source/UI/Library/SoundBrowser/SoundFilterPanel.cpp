// SoundFilterPanel.cpp -- the controls of the Filters panel, how they are laid out one group under another, and how
// each one writes to the SoundFilterModel.
#include "UI/Library/SoundBrowser/SoundFilterPanel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

using synth::soundbrowser::SoundKind;
using synth::soundbrowser::SoundLength;

namespace {

constexpr int kLabelHeight = 16;
constexpr int kControlHeight = 26;
constexpr int kGroupGap = 8;
constexpr int kAnyId = 1;
constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

} // namespace

SoundFilterPanel::SoundFilterPanel(SoundFilterModel& model)
    : model_(model) {
    setTitle("Filters");
    kind_.setItems({{"0", SoundFilterModel::kindWord(SoundKind::OneShot), false},
                    {"1", SoundFilterModel::kindWord(SoundKind::Loop), false},
                    {"2", SoundFilterModel::kindWord(SoundKind::Instrument), false}});
    sound_.setItems({{"Drums", "Drums", false},
                     {"Bass", "Bass", false},
                     {"Keys", "Keys", false},
                     {"Vocals", "Vocals", false},
                     {"FX", "FX", false},
                     {"Strings", "Strings", false}});
    nearTempo_.setItems({{"near", "Near song tempo", false}});
    songKey_.setItems({{"song", "Song key", false}});
    nearTempo_.setTooltip("Only sounds within 10 BPM of the song's tempo, or of half or double it");
    songKey_.setTooltip("Only sounds in the song's key");

    kind_.onToggled = [this](const juce::String& id, bool on) {
        model_.setKind((SoundKind)id.getIntValue(), on);
        changed();
    };
    sound_.onToggled = [this](const juce::String& id, bool on) {
        model_.setTag(id, on);
        changed();
    };
    nearTempo_.onToggled = [this](const juce::String&, bool on) {
        model_.setNearSongTempo(on);
        changed();
    };
    songKey_.onToggled = [this](const juce::String&, bool on) {
        model_.setUseSongKey(on);
        syncFromModel();
        changed();
    };

    tempo_.setLimits(SoundFilterModel::kMinTempo, SoundFilterModel::kMaxTempo, 1.0);
    tempo_.setLabel("Tempo", "BPM");
    tempo_.setValues(SoundFilterModel::kMinTempo, SoundFilterModel::kMaxTempo);
    tempo_.onChange = [this](double lo, double hi) { tempoChanged(lo, hi); };

    keyBox_.setTitle("Key filter");
    keyBox_.setTooltip("Only sounds in this key");
    keyBox_.addItem("Any key", kAnyId);
    for (int minor = 0; minor < 2; ++minor)
        for (int pc = 0; pc < 12; ++pc)
            keyBox_.addItem(juce::String(kKeyNames[pc]) + (minor ? " minor" : " major"), 2 + minor * 12 + pc);
    keyBox_.setSelectedId(kAnyId, juce::dontSendNotification);
    keyBox_.onChange = [this] { keyChosen(); };

    lengthBox_.setTitle("Length filter");
    lengthBox_.setTooltip("Only sounds of this length");
    for (int i = 0; i < 4; ++i)
        lengthBox_.addItem(SoundFilterModel::lengthWord((SoundLength)i), 1 + i);
    lengthBox_.setSelectedId(1, juce::dontSendNotification);
    lengthBox_.onChange = [this] { lengthChosen(); };

    placeBox_.setTitle("Place filter");
    placeBox_.setTooltip("Only sounds from this pack or folder");
    placeBox_.addItem("Any pack or folder", kAnyId);
    placeBox_.setSelectedId(kAnyId, juce::dontSendNotification);
    placeBox_.onChange = [this] { placeChosen(); };

    for (juce::Component* c : std::initializer_list<juce::Component*>{&kind_, &sound_, &tempo_, &nearTempo_, &songKey_,
                                                                      &keyBox_, &lengthBox_, &placeBox_})
        addAndMakeVisible(*c);
    syncFromModel();
}

void SoundFilterPanel::changed() {
    updateTempoReadout();
    if (!syncing_ && onChanged)
        onChanged();
}

void SoundFilterPanel::tempoChanged(double lo, double hi) {
    const bool full = lo <= SoundFilterModel::kMinTempo && hi >= SoundFilterModel::kMaxTempo;
    model_.setTempoRange(full ? std::nullopt : std::optional<std::pair<int, int>>(std::make_pair((int)lo, (int)hi)));
    changed();
}

void SoundFilterPanel::updateTempoReadout() {
    const auto range = model_.tempoRange();
    tempoReadoutText_ = range.has_value() ? juce::String(range->first) + "-" + juce::String(range->second) + " BPM"
                                          : juce::String("Any tempo");
    repaint(tempoReadout_);
}

void SoundFilterPanel::keyChosen() {
    const int id = keyBox_.getSelectedId();
    if (id <= kAnyId) {
        model_.setPickedKey({});
    } else {
        const int index = id - 2;
        model_.setPickedKey(juce::String(kKeyNames[index % 12]) + (index >= 12 ? " minor" : " major"));
    }
    syncFromModel();
    changed();
}

void SoundFilterPanel::lengthChosen() {
    model_.setLength((SoundLength)(lengthBox_.getSelectedId() - 1));
    changed();
}

void SoundFilterPanel::placeChosen() {
    const int id = placeBox_.getSelectedId();
    if (id <= kAnyId)
        model_.setPlace({}, {});
    else if (id - 2 < (int)places_.size())
        model_.setPlace(places_[(size_t)(id - 2)].first, places_[(size_t)(id - 2)].second);
    changed();
}

void SoundFilterPanel::setPlaces(std::vector<std::pair<juce::String, juce::String>> places) {
    places_ = std::move(places);
    const juce::ScopedValueSetter<bool> guard(syncing_, true);
    placeBox_.clear(juce::dontSendNotification);
    placeBox_.addItem("Any pack or folder", kAnyId);
    int selected = kAnyId;
    for (int i = 0; i < (int)places_.size(); ++i) {
        placeBox_.addItem(places_[(size_t)i].second, 2 + i);
        if (places_[(size_t)i].first == model_.placeId())
            selected = 2 + i;
    }
    placeBox_.setSelectedId(selected, juce::dontSendNotification);
}

void SoundFilterPanel::syncFromModel() {
    const juce::ScopedValueSetter<bool> guard(syncing_, true);
    for (auto kind : {SoundKind::OneShot, SoundKind::Loop, SoundKind::Instrument})
        kind_.setOn(juce::String((int)kind), model_.hasKind(kind));
    for (const char* tag : {"Drums", "Bass", "Keys", "Vocals", "FX", "Strings"})
        sound_.setOn(tag, model_.hasTag(tag));
    const auto range = model_.tempoRange();
    tempo_.setValues(range ? range->first : SoundFilterModel::kMinTempo,
                     range ? range->second : SoundFilterModel::kMaxTempo);
    nearTempo_.setOn("near", model_.nearSongTempo());
    const bool haveSongKey = model_.songKey().isNotEmpty();
    songKey_.setItems({{"song", "Song key, " + model_.songKey(), model_.useSongKey()}});
    songKey_.setVisible(haveSongKey);
    int keyId = kAnyId;
    if (model_.pickedKey().isNotEmpty())
        for (int i = 0; i < keyBox_.getNumItems(); ++i)
            if (keyBox_.getItemText(i) == model_.pickedKey())
                keyId = keyBox_.getItemId(i);
    keyBox_.setSelectedId(keyId, juce::dontSendNotification);
    lengthBox_.setSelectedId(1 + (int)model_.length(), juce::dontSendNotification);
    int placeId = kAnyId;
    for (int i = 0; i < (int)places_.size(); ++i)
        if (places_[(size_t)i].first == model_.placeId())
            placeId = 2 + i;
    placeBox_.setSelectedId(placeId, juce::dontSendNotification);
    updateTempoReadout();
    resized();
}

// One group under another; with `apply` false it only measures.
void SoundFilterPanel::layoutRows(int width, bool apply) {
    const int inner = juce::jmax(40, width - 2 * kPad);
    int y = kPad;
    labels_.clear();
    const auto label = [&](const juce::String& text) {
        if (apply)
            labels_.push_back({text, {kPad, y, inner, kLabelHeight}});
        y += kLabelHeight;
    };
    const auto place = [&](juce::Component& c, int height) {
        if (apply)
            c.setBounds(kPad, y, inner, height);
        y += height + kGroupGap;
    };
    label("Kind");
    place(kind_, kind_.heightForWidth(inner));
    label("Sound");
    place(sound_, sound_.heightForWidth(inner));
    label("Tempo");
    if (apply)
        tempoReadout_ = {kPad + inner / 2, y - kLabelHeight, inner / 2, kLabelHeight};
    place(tempo_, kControlHeight);
    y -= kGroupGap - 2;
    place(nearTempo_, nearTempo_.heightForWidth(inner));
    label("Key");
    if (songKey_.isVisible()) {
        place(songKey_, songKey_.heightForWidth(inner));
        y -= kGroupGap - 2;
    }
    place(keyBox_, kControlHeight);
    const int half = (inner - kGroupGap) / 2;
    if (apply) {
        labels_.push_back({"Length", {kPad, y, half, kLabelHeight}});
        labels_.push_back({"Place", {kPad + half + kGroupGap, y, half, kLabelHeight}});
    }
    y += kLabelHeight;
    if (apply) {
        lengthBox_.setBounds(kPad, y, half, kControlHeight);
        placeBox_.setBounds(kPad + half + kGroupGap, y, inner - half - kGroupGap, kControlHeight);
    }
    layoutHeight_ = y + kControlHeight + kPad;
}

int SoundFilterPanel::heightForWidth(int width) const {
    auto* self = const_cast<SoundFilterPanel*>(this);
    self->layoutRows(width, false);
    return layoutHeight_;
}

void SoundFilterPanel::resized() {
    layoutRows(getWidth(), true);
    updateTempoReadout();
}

void SoundFilterPanel::paint(juce::Graphics& g) {
    const auto& c = synth::theme::themeOf(*this).colors;
    g.fillAll(c.surface);
    g.setColour(c.border);
    g.fillRect(getLocalBounds().removeFromBottom(1));
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.setColour(c.textMuted);
    for (const auto& l : labels_)
        g.drawText(l.text, l.area, juce::Justification::centredLeft, true);
    g.setColour(c.textPrimary);
    g.drawText(tempoReadoutText_, tempoReadout_, juce::Justification::centredRight, true);
}

bool SoundFilterPanel::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey) && !key.getModifiers().isAnyModifierKeyDown()) {
        if (onCloseRequested)
            onCloseRequested();
        return true;
    }
    return false;
}

} // namespace synth::ui
