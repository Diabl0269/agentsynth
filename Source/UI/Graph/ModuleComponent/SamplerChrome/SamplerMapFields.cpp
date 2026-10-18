// SamplerMapFields.cpp -- the detail panel of the instrument map window.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapFields.h"
#include "Modules/Sampler/SamplerNoteNames.h"
#include "Modules/Sampler/SamplerRegionEdit.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kColumns = 4;
constexpr int kGap = 10;
constexpr int kFieldHeight = 24;
constexpr int kCaptionHeight = 16;
constexpr int kRowHeight = kCaptionHeight + kFieldHeight + 6;
constexpr int kButtonHeight = 26;
constexpr int kButtonWidth = 112;
constexpr float kCaptionFontSize = 11.0f;
constexpr double kMaxTuneCents = 1200.0;
constexpr double kMinVolumeDb = -48.0;
constexpr double kMaxVolumeDb = 24.0;
constexpr int kMiddleCOctave = 3;

struct FieldInfo {
    const char* name;
    const char* tooltip;
    double lo, hi, step;
};

const FieldInfo kInfo[SamplerMapFields::NumFields] = {
    {"Low key", "Lowest key this recording plays", 0, 127, 1},
    {"High key", "Highest key this recording plays", 0, 127, 1},
    {"Root key", "The key the recording was played at; it sounds unchanged there", 0, 127, 1},
    {"Low velocity", "Softest hit this recording answers (1 to 127)", 1, 127, 1},
    {"High velocity", "Hardest hit this recording answers (1 to 127)", 1, 127, 1},
    {"Volume (dB)", "How much louder or quieter this recording plays, in decibels", kMinVolumeDb, kMaxVolumeDb, 0.1},
    {"Tuning (cents)", "Fine tuning of this recording in cents; 100 cents is one semitone", -kMaxTuneCents,
     kMaxTuneCents, 1},
};

bool isKeyField(int field) { return field <= SamplerMapFields::RootKey; }

} // namespace

SamplerMapFields::SamplerMapFields() {
    setWantsKeyboardFocus(false);
    std::vector<juce::Component*> detail{&fileLabel_, &replaceButton_, &removeButton_};
    for (int f = 0; f < NumFields; ++f) {
        fields_[(size_t)f] = std::make_unique<juce::Slider>(juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
        styleField((Field)f, kInfo[f].name, kInfo[f].tooltip);
        captions_[(size_t)f].setText(kInfo[f].name, juce::dontSendNotification);
        captions_[(size_t)f].setFont(juce::Font(juce::FontOptions(kCaptionFontSize)));
        captions_[(size_t)f].setAccessible(false);
        addAndMakeVisible(captions_[(size_t)f]);
        addAndMakeVisible(*fields_[(size_t)f]);
        detail.push_back(fields_[(size_t)f].get());
        detail.push_back(&captions_[(size_t)f]);
    }
    fileLabel_.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    fileLabel_.setTitle("Recording file");
    replaceButton_.setTitle("Replace file");
    replaceButton_.setTooltip("Replace this recording with another audio file");
    removeButton_.setTitle("Remove recording");
    removeButton_.setTooltip("Remove this recording from the instrument (Delete)");
    replaceButton_.onClick = [this] {
        if (onReplaceFile)
            onReplaceFile();
    };
    removeButton_.onClick = [this] {
        if (onRemove)
            onRemove();
    };
    hint_.setJustificationType(juce::Justification::centred);
    for (juce::Component* c : {(juce::Component*)&fileLabel_, (juce::Component*)&replaceButton_,
                               (juce::Component*)&removeButton_, (juce::Component*)&hint_})
        addAndMakeVisible(*c);
    detailFade_ = std::make_unique<FadeVisibility>(std::move(detail));
    hintFade_ = std::make_unique<FadeVisibility>(std::vector<juce::Component*>{&hint_});
    detailFade_->snapTo(false);
    hintFade_->snapTo(true);
}

SamplerMapFields::~SamplerMapFields() = default;

void SamplerMapFields::styleField(Field field, const juce::String& name, const juce::String& tooltip) {
    auto& s = *fields_[(size_t)field];
    const auto& info = kInfo[field];
    s.setRange(info.lo, info.hi, info.step);
    s.setTitle(name);
    s.setTooltip(tooltip);
    s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 64, kFieldHeight);
    s.setNumDecimalPlacesToDisplay(info.step < 1.0 ? 1 : 0);
    if (isKeyField(field)) {
        s.textFromValueFunction = [](double v) { return synth::sampler::keyName((int)v); };
        s.valueFromTextFunction = [&s](const juce::String& text) {
            return (double)synth::sampler::parseNoteName(text, kMiddleCOctave).value_or((int)s.getValue());
        };
    }
    s.onValueChange = [this, field] { fieldChanged(field); };
}

void SamplerMapFields::setValuesQuietly() {
    const double values[NumFields] = {(double)region_.loKey,    (double)region_.hiKey, (double)region_.rootKey,
                                      (double)region_.loVel,    (double)region_.hiVel, (double)region_.volumeDb,
                                      (double)region_.tuneCents};
    for (int f = 0; f < NumFields; ++f)
        fields_[(size_t)f]->setValue(values[f], juce::dontSendNotification);
}

void SamplerMapFields::showRegion(const synth::sampler::RegionSpec* region) {
    hasRegion_ = region != nullptr;
    if (region != nullptr) {
        region_ = *region;
        setValuesQuietly();
        fileLabel_.setText(juce::File(region_.samplePath).getFileName(), juce::dontSendNotification);
        fileLabel_.setTooltip(region_.samplePath);
    }
    detailFade_->setShown(hasRegion_);
    hintFade_->setShown(!hasRegion_);
}

void SamplerMapFields::setEditable(bool editable) {
    editable_ = editable;
    for (auto& f : fields_)
        f->setEnabled(editable);
    replaceButton_.setEnabled(editable);
    removeButton_.setEnabled(editable);
}

void SamplerMapFields::focusFirstField() {
    if (hasRegion_ && editable_)
        fields_[LowKey]->grabKeyboardFocus();
}

// Only the field that changed is read, so a value the slider rounds (0 dB shown as a hair off zero) never leaks into
// the others. The low key never rises above the high key (and the other way round): the field not being edited gives
// way.
void SamplerMapFields::fieldChanged(Field field) {
    if (!hasRegion_)
        return;
    const double value = fields_[(size_t)field]->getValue();
    switch (field) {
    case LowKey:
        region_.loKey = (int)value;
        region_.hiKey = juce::jmax(region_.hiKey, region_.loKey);
        break;
    case HighKey:
        region_.hiKey = (int)value;
        region_.loKey = juce::jmin(region_.loKey, region_.hiKey);
        break;
    case RootKey:
        region_.rootKey = (int)value;
        break;
    case LowVelocity:
        region_.loVel = (int)value;
        region_.hiVel = juce::jmax(region_.hiVel, region_.loVel);
        break;
    case HighVelocity:
        region_.hiVel = (int)value;
        region_.loVel = juce::jmin(region_.loVel, region_.hiVel);
        break;
    case Volume:
        region_.volumeDb = (float)(std::round(value * 10.0) / 10.0);
        break;
    case Tuning:
        region_.tuneCents = (float)std::round(value);
        break;
    case NumFields:
        break;
    }
    setValuesQuietly();
    if (onChange)
        onChange(region_);
}

void SamplerMapFields::resized() {
    auto area = getLocalBounds();
    hint_.setBounds(area);
    auto top = area.removeFromTop(kButtonHeight);
    removeButton_.setBounds(top.removeFromRight(kButtonWidth - 40));
    top.removeFromRight(kGap);
    replaceButton_.setBounds(top.removeFromRight(kButtonWidth));
    top.removeFromRight(kGap);
    fileLabel_.setBounds(top);
    area.removeFromTop(kGap);
    const int columnWidth = (area.getWidth() - (kColumns - 1) * kGap) / kColumns;
    for (int f = 0; f < NumFields; ++f) {
        const int column = f % kColumns;
        const int row = f / kColumns;
        const int x = area.getX() + column * (columnWidth + kGap);
        const int y = area.getY() + row * kRowHeight;
        captions_[(size_t)f].setBounds(x, y, columnWidth, kCaptionHeight);
        fields_[(size_t)f]->setBounds(x, y + kCaptionHeight, columnWidth, kFieldHeight);
    }
}

} // namespace synth::ui
