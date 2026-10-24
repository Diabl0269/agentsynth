// Concern: the Audio tab's Remembered interfaces section.
#include "UI/Settings/AudioSettingsTab/AudioInterfacesList.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace {
constexpr int kRowHeight = 30;
constexpr float kDotSize = 8.0f;
constexpr int kForgetWidth = 60;
juce::String dot() { return juce::String::fromUTF8(" \xC2\xB7 "); }
} // namespace

AudioInterfacesList::Row::Row(juce::String type, juce::String name)
    : deviceType(std::move(type))
    , deviceName(std::move(name)) {
    setInterceptsMouseClicks(false, true);
}

void AudioInterfacesList::Row::resized() {
    if (forget != nullptr)
        forget->setBounds(getLocalBounds().removeFromRight(kForgetWidth).reduced(0, 4));
}

void AudioInterfacesList::Row::paint(juce::Graphics& g) {
    const auto& colours = synth::theme::themeOf(*this).colors;
    auto area = getLocalBounds().toFloat();
    g.setColour(inUse ? colours.success : colours.textDisabled);
    g.fillEllipse(juce::Rectangle<float>(kDotSize, kDotSize).withCentre({10.0f, area.getCentreY()}));
    const auto nameFont = juce::Font(juce::FontOptions(13.0f, inUse ? juce::Font::bold : juce::Font::plain));
    g.setFont(nameFont);
    g.setColour(colours.textPrimary);
    const int nameWidth =
        juce::jmin(getWidth() / 2, juce::GlyphArrangement::getStringWidthInt(nameFont, deviceName) + 4);
    g.drawText(deviceName, juce::Rectangle<int>(24, 0, nameWidth, getHeight()), juce::Justification::centredLeft, true);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.setColour(inUse ? colours.textMuted : colours.warning);
    const int statusLeft = 24 + nameWidth + 12;
    g.drawText(status,
               juce::Rectangle<int>(statusLeft, 0, juce::jmax(0, getWidth() - statusLeft - kForgetWidth), getHeight()),
               juce::Justification::centredLeft, true);
}

AudioInterfacesList::AudioInterfacesList(AudioSettingsSource& source)
    : source_(source) {
    empty_.setText("No audio interface has been used yet.", juce::dontSendNotification);
    addChildComponent(empty_);
    refresh();
}

int AudioInterfacesList::preferredHeight(int) const { return kPad * 2 + juce::jmax(1, (int)rows_.size()) * kRowHeight; }

juce::String AudioInterfacesList::summary() const {
    return juce::String((int)rows_.size()) + (rows_.size() == 1 ? " interface" : " interfaces");
}

// Rows are kept per interface and updated in place; one that is no longer in the store fades out.
void AudioInterfacesList::refresh() {
    const auto current = source_.currentInterface();
    const auto entries = source_.layoutStore().entries();

    for (auto it = rows_.begin(); it != rows_.end();) {
        const bool stays = std::any_of(entries.begin(), entries.end(), [&](const auto& e) {
            return e.deviceType == (*it)->deviceType && e.deviceName == (*it)->deviceName;
        });
        if (stays) {
            ++it;
            continue;
        }
        leaving_.retire(std::move(*it));
        it = rows_.erase(it);
    }

    std::vector<std::unique_ptr<Row>> ordered;
    for (const auto& entry : entries) {
        auto found = std::find_if(rows_.begin(), rows_.end(), [&](const auto& r) {
            return r->deviceType == entry.deviceType && r->deviceName == entry.deviceName;
        });
        std::unique_ptr<Row> row;
        if (found != rows_.end()) {
            row = std::move(*found);
            rows_.erase(found);
        } else {
            row = std::make_unique<Row>(entry.deviceType, entry.deviceName);
            addAndMakeVisible(*row);
        }
        row->inUse = current.deviceName.isNotEmpty() && entry.deviceType == current.deviceType &&
                     entry.deviceName == current.deviceName;
        if (row->inUse) {
            const int named =
                source_.layoutStore().countNamedInputs(entry.deviceType, entry.deviceName, current.numInputs);
            row->status = "in use" + dot() +
                          (named == 0 ? juce::String("no inputs named")
                                      : juce::String(named) + (named == 1 ? " input named" : " inputs named"));
            row->forget.reset();
        } else {
            row->status = "not connected" + dot() + "names kept";
            if (row->forget == nullptr) {
                row->forget = std::make_unique<synth::ui::TextLinkButton>("Forget", juce::Justification::centredRight);
                row->forget->setTitle("Forget " + entry.deviceName);
                row->forget->setTooltip("Forget " + entry.deviceName + ": remove its saved pairs and input names");
                row->forget->setWantsKeyboardFocus(true);
                row->forget->onClick = [this, type = entry.deviceType, name = entry.deviceName] {
                    source_.layoutStore().forget(type, name);
                };
                row->addAndMakeVisible(*row->forget);
            }
        }
        row->resized();
        row->repaint();
        ordered.push_back(std::move(row));
    }
    rows_ = std::move(ordered);
    empty_.setVisible(rows_.empty());
    resized();
}

void AudioInterfacesList::resized() {
    auto area = getLocalBounds().reduced(kPad);
    empty_.setBounds(area.removeFromTop(kRowHeight));
    for (auto& row : rows_)
        row->setBounds(area.removeFromTop(kRowHeight));
}
