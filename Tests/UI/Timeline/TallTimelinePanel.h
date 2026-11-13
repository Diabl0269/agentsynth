#pragma once

// TallTimelinePanel.h -- a timeline panel tall enough that the lanes a reorder drag aims at sit clear of the track
// list's edge zones, where a drag scrolls the list (docs/layout/layout.md, "Every drag follows the view"); at the
// default height the list is about a hundred pixels tall and nearly every target is in a zone. Make it the FIRST base
// of a scene, so the height is saved before the scene's MainComponent reads it; the old value goes back afterwards.
// Header-only; not registered in Tests/CMakeLists.txt.

#include "../../TestSettingsHelpers.h"
#include "MainComponent/MainComponent.h"

struct TallTimelinePanel {
    static constexpr int kHeight = 600;
    synth::test::PersistedKeysGuard keep{juce::StringArray{MainComponent::kTimelinePanelHeightKey}};
    TallTimelinePanel() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::test::userSettingsTestOptions());
        if (auto* settings = props.getUserSettings()) {
            settings->setValue(MainComponent::kTimelinePanelHeightKey, kHeight);
            settings->saveIfNeeded();
        }
    }
};
