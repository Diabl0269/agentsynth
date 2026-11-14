#pragma once

#include "SoundBrowser/SoundFolderBook.h"
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
    , private synth::soundpacks::SoundPackInstaller::Listener
    , private synth::soundbrowser::SoundFolderBook::Listener {
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
    /** Glides the Sounds width to the default over 190 ms (a drag, a key step or a new width stops the glide); under
     *  Reduce Motion, Animations Off, or off screen it jumps. The width is saved once, where it lands. */
    void resetSoundsWidth();
    bool isWidthGlideRunning() const { return widthAnim_.isRunning(); }
    /** Test seams: run the glide while the column is off screen, and step it to `ms` after its first frame. */
    void setAnimateOffscreenForTest(bool on) { animateOffscreenForTest_ = on; }
    void stepWidthGlideForTest(double ms) { widthAnim_.stepForTest(ms); }
    void completeWidthGlideForTest() { widthAnim_.completeForTest(); }
    bool isViewMotionRunning() const { return anim_.isRunning(); }
    void completeViewMotionForTest() { anim_.completeForTest(); }

    /** Lands keyboard focus on the showing view's search field (the Cmd+F shortcut), not on the column's root. */
    void focusShowingViewSearch();
    /** Takes keyboard focus to the Modules / Sounds switch (Down from the root, Up from a view's first control). */
    void focusSwitch();

    void setShortcutHints(const juce::String& modulesKey, const juce::String& soundsKey);

    SoundBrowserComponent& getSounds() { return sounds_; }
    LibrarySidebarSwitch& getSwitch() { return switch_; }
    EdgeResizeHandle& getResizeHandle() { return handle_; }
    synth::soundbrowser::SoundIndex& getIndex() { return index_; }
    synth::soundbrowser::SoundLists& getLists() { return lists_; }
    /** The user's own folders: edits are saved to the settings and handed to the index (which scans once Sounds has
     *  shown). */
    synth::soundbrowser::SoundFolderBook& getFolderBook() { return folderBook_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void paintOverChildren(juce::Graphics& g) override;
    /** The column's own keys, reached from the root and bubbling from a child that ignores the key: Down on the root
     *  enters the switch and Down on the switch the view's search field; Up on the switch returns to the root; Left and
     *  Right on the root change the view; Escape from anywhere inside returns to the root. */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void layoutViews();
    void notifyLayout();
    void ensureIndexStarted();
    void persistLists();
    void persistPreviewSettings();
    void soundPacksChanged() override;
    void soundFoldersChanged() override;
    void commitWidth();
    void setSoundsWidthNow(int width, bool persist); // sets and notifies; leaves a running glide alone

    juce::PropertiesFile* settings_;
    ModuleLibraryComponent& modules_;
    synth::soundbrowser::SoundIndex index_;
    synth::soundbrowser::SoundFolderBook folderBook_{index_};
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
    bool modulesTookFocus_ = true;
    juce::String installedSignature_;
    std::optional<juce::VBlankAnimatorUpdater> vblank_;
    AnimationDriver anim_;
    AnimationDriver widthAnim_; // the double-click reset's glide; apart from anim_ so neither stops the other
    bool animateOffscreenForTest_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrarySidebarViews)
};

} // namespace synth::ui
