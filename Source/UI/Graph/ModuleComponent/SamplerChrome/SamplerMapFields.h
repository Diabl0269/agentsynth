#pragma once

// The map window's detail panel: the selected recording's file, keys, velocity range, volume and tuning, each a field
// that changes the recording as it is committed (Enter, the arrows or the step buttons).

#include "Modules/Sampler/SamplerInstrument.h"
#include "UI/Layout/FadeVisibility.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class SamplerMapFields : public juce::Component {
public:
    enum Field { LowKey, HighKey, RootKey, LowVelocity, HighVelocity, Volume, Tuning, NumFields };

    SamplerMapFields();
    ~SamplerMapFields() override;

    /** Shows `region` (null shows the hint to select one) without telling onChange. */
    void showRegion(const synth::sampler::RegionSpec* region);
    /** False: the fields show their values but cannot be changed. */
    void setEditable(bool editable);

    juce::Slider& getField(Field field) { return *fields_[field]; }
    juce::TextButton& getReplaceButton() noexcept { return replaceButton_; }
    juce::TextButton& getRemoveButton() noexcept { return removeButton_; }
    const juce::Label& getFileLabel() const noexcept { return fileLabel_; }
    /** Moves keyboard focus to Low key. */
    void focusFirstField();

    /** The edited recording after a field changed (the keys kept in order: low never above high). */
    std::function<void(const synth::sampler::RegionSpec&)> onChange;
    std::function<void()> onReplaceFile;
    std::function<void()> onRemove;

    void resized() override;

private:
    void fieldChanged(Field field);
    void styleField(Field field, const juce::String& name, const juce::String& tooltip);
    void setValuesQuietly();

    synth::sampler::RegionSpec region_;
    bool hasRegion_ = false;
    bool editable_ = true;
    std::array<std::unique_ptr<juce::Slider>, NumFields> fields_;
    std::array<juce::Label, NumFields> captions_;
    juce::Label fileLabel_{"File", juce::String()};
    juce::Label hint_{"Hint", "Select a recording above to edit it."};
    juce::TextButton replaceButton_{"Replace file..."};
    juce::TextButton removeButton_{"Remove"};
    std::unique_ptr<FadeVisibility> detailFade_; // the file row, the captions and the fields
    std::unique_ptr<FadeVisibility> hintFade_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerMapFields)
};

} // namespace synth::ui
