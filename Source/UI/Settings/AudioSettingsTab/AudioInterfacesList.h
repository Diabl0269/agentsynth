#pragma once

#include "UI/Layout/TextLinkButton.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSection.h"
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"
#include "UI/Settings/AudioSettingsTab/LeavingRows.h"
#include <memory>
#include <vector>

// The Remembered interfaces section: one row per interface ever used -- a dot (green in use, grey not), the name
// (bold in use) and a status: "in use . 3 inputs named", or, in the warning colour, "not connected . names kept". A
// Forget link on every row but the one in use removes that interface's saved pairs and names.
class AudioInterfacesList : public AudioSettingsSection {
public:
    explicit AudioInterfacesList(AudioSettingsSource& source);

    int preferredHeight(int width) const override;
    juce::String summary() const override;
    void refresh() override;
    void resized() override;

    struct Row : juce::Component {
        Row(juce::String type, juce::String name);
        void paint(juce::Graphics& g) override;
        void resized() override;
        juce::String deviceType, deviceName;
        bool inUse = false;
        juce::String status;
        std::unique_ptr<synth::ui::TextLinkButton> forget;
    };

    int getNumRowsForTest() const { return (int)rows_.size(); }
    Row& getRowForTest(int index) { return *rows_[(size_t)index]; }
    int getNumLeavingForTest() const { return leaving_.count(); }

private:
    AudioSettingsSource& source_;
    std::vector<std::unique_ptr<Row>> rows_;
    LeavingRows leaving_;
    juce::Label empty_;
};
