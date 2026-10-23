#pragma once

// One of the user's own sound folders in Settings > Sound packs: its name, its path and a Remove button.

#include "SoundBrowser/SoundFolderBook.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class SoundFolderRow : public juce::Component {
public:
    static constexpr int kHeight = 48;

    /** `onRemove` is called with the folder's path from the Remove button. */
    SoundFolderRow(const synth::soundbrowser::UserFolder& folder, std::function<void(const juce::File&)> onRemove);

    const juce::File& getPath() const noexcept { return path_; }
    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

    // ---- Test seams ----
    juce::TextButton& getRemoveButton() noexcept { return remove_; }
    juce::String getNameText() const { return name_.getText(); }
    juce::String getPathText() const { return path_line_.getText(); }

private:
    void applyColours();

    juce::File path_;
    juce::Label name_, path_line_;
    juce::TextButton remove_{"Remove"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundFolderRow)
};

} // namespace synth::ui
