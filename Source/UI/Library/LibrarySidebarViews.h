#pragma once

#include "SoundBrowser/SoundIndex.h"
#include "SoundBrowser/SoundLists.h"
#include "SoundPacks/SoundPackInstaller.h"
#include "UI/Layout/EdgeResizeHandle.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/LibrarySidebarSwitch.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <optional>

namespace synth::ui {

/** The left sidebar's column: the Modules / Sounds switch on top and the two views below it. It owns the Sounds view,
 *  the sound index and the favourites, adopts the module library as its other child, remembers which view shows and
 *  how wide the Sounds view is, and slides between the views (docs/layout/sound-browser.md). It is the "library"
 *  focus region's root. */
class LibrarySidebarViews
    : public juce::Component
    , public juce::SettableTooltipClient
    , private synth::soundpacks::SoundPackInstaller::Listener {
public:
    static constexpr int kDefaultSoundsWidth = 280;
    static constexpr int kMinSoundsWidth = 200;
    static constexpr int kMaxSoundsWidth = 400;
    static constexpr int kWidthKeyStep = 16;

    /** `settings` may be null (nothing persists). Adopts `modules` as a child. */
    LibrarySidebarViews(ModuleLibraryComponent& modules, juce::PropertiesFile* settings);
    ~LibrarySidebarViews() override;

    /** The column's width or the views' offsets changed: the owner lays out again. */
    std::function<void()> onLayoutChanged;

    SidebarView getView() const { return view_; }
    /** Switches the view (slides when `animate` and on screen) and remembers it. */
    void setView(SidebarView view, bool animate);
    /** The column's open width: the theme's `modulesWidth` while Modules shows, the Sounds width while Sounds shows,
     *  eased between the two while the views slide. */
    int openWidth(int modulesWidth) const;
    int getSoundsWidth() const { return soundsWidth_; }
    void setSoundsWidth(int width, bool persist);
    bool isViewMotionRunning() const { return anim_.isRunning(); }
    void completeViewMotionForTest() { anim_.completeForTest(); }

    /** Lands keyboard focus on the showing view's own focus target. */
    void focusShowingView();
    void focusShowingViewSearch();

    void setShortcutHints(const juce::String& modulesKey, const juce::String& soundsKey);

    SoundBrowserComponent& getSounds() { return sounds_; }
    LibrarySidebarSwitch& getSwitch() { return switch_; }
    EdgeResizeHandle& getResizeHandle() { return handle_; }
    synth::soundbrowser::SoundIndex& getIndex() { return index_; }
    synth::soundbrowser::SoundLists& getLists() { return lists_; }
    /** Persists the folder list, hands it to the index and rescans. */
    void setUserFolders(const std::vector<juce::File>& folders);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void focusGained(FocusChangeType cause) override;

private:
    void layoutViews();
    void notifyLayout();
    void ensureIndexStarted();
    void persistLists();
    void soundPacksChanged() override;
    void commitWidth();

    juce::PropertiesFile* settings_;
    ModuleLibraryComponent& modules_;
    synth::soundbrowser::SoundIndex index_;
    synth::soundbrowser::SoundLists lists_;
    SoundBrowserComponent sounds_;
    LibrarySidebarSwitch switch_;
    EdgeResizeHandle handle_{EdgeResizeHandle::Axis::Horizontal};
    juce::SharedResourcePointer<synth::soundpacks::SoundPackInstaller> installer_;

    SidebarView view_ = SidebarView::Modules;
    PanelSlide mix_; // 0 = Modules showing, 1 = Sounds showing
    int soundsWidth_ = kDefaultSoundsWidth;
    int widthAtDragStart_ = kDefaultSoundsWidth;
    bool indexStarted_ = false;
    juce::String installedSignature_;
    std::optional<juce::VBlankAnimatorUpdater> vblank_;
    AnimationDriver anim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrarySidebarViews)
};

} // namespace synth::ui
