// LibrarySidebarViews.cpp -- the left column's two views: which one shows (and its slide), the Sounds width and its
// drag handle, the settings round trip, and the index's lifetime.
#include "UI/Library/LibrarySidebarViews.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UserSettings.h"

namespace synth::ui {

namespace {

juce::File indexCacheFile() {
    return synth::userSettingsRootDirectory().getChildFile("SoundBrowser").getChildFile("index.json");
}

} // namespace

LibrarySidebarViews::LibrarySidebarViews(ModuleLibraryComponent& modules, juce::PropertiesFile* settings)
    : settings_(settings)
    , modules_(modules)
    , index_(indexCacheFile())
    , sounds_(index_, lists_) {
    setTitle("Library");
    setTooltip("Library: Modules and Sounds. Down reaches the view switch; Left and Right change the view.");
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);

    // The column's root is the region's focus holder now; the module library keeps its search field as its one stop
    // (its rows are a cursor kept there), so it no longer takes focus itself.
    modulesTookFocus_ = modules_.getWantsKeyboardFocus();
    modules_.setWantsKeyboardFocus(false);
    addChildComponent(modules_);
    addChildComponent(sounds_);
    addAndMakeVisible(switch_);
    addChildComponent(handle_);

    if (settings_ != nullptr) {
        view_ = settings_->getValue(kLibrarySidebarViewSettingKey, "modules") == "sounds" ? SidebarView::Sounds
                                                                                          : SidebarView::Modules;
        soundsWidth_ = juce::jlimit(kMinSoundsWidth, kMaxSoundsWidth,
                                    settings_->getIntValue(kSoundBrowserWidthSettingKey, kDefaultSoundsWidth));
        lists_.setFavourites(
            synth::soundbrowser::SoundLists::fromSetting(settings_->getValue(kSoundBrowserFavouritesSettingKey)));
        lists_.setRecent(
            synth::soundbrowser::SoundLists::fromSetting(settings_->getValue(kSoundBrowserRecentSettingKey)));
        folderBook_.load(settings_->getValue(kSoundBrowserFoldersSettingKey),
                         settings_->getValue(kSoundBrowserFolderNamesSettingKey));
    }
    if (settings_ != nullptr) {
        using Player = synth::soundbrowser::SoundPreviewPlayer;
        sounds_.setPreviewSettings(settings_->getBoolValue(kSoundBrowserAutoPlaySettingKey, true),
                                   juce::jlimit(Player::kMinGainDb, Player::kMaxGainDb,
                                                (float)settings_->getDoubleValue(kSoundBrowserPreviewGainDbSettingKey,
                                                                                 Player::kDefaultGainDb)),
                                   settings_->getBoolValue(kSoundBrowserMatchKeySettingKey, false));
    }
    if (settings_ != nullptr)
        sounds_.setGetMoreOpen(settings_->getBoolValue(kSoundBrowserGetMoreOpenSettingKey, true), false);
    sounds_.onGetMoreChanged = [this] {
        if (settings_ == nullptr)
            return;
        settings_->setValue(kSoundBrowserGetMoreOpenSettingKey, sounds_.isGetMoreOpen());
        settings_->saveIfNeeded();
    };
    sounds_.onPreviewSettingsChanged = [this] { persistPreviewSettings(); };
    lists_.onChanged = [this] { persistLists(); };
    mix_.snapTo(view_ == SidebarView::Sounds ? 1.0f : 0.0f);
    switch_.setView(view_, false);
    switch_.onViewChosen = [this](SidebarView v) { setView(v, true); };
    sounds_.onUpFromSearch = [this] {
        focusSwitch();
        return true;
    };
    modules_.onUpFromTop = [this] {
        focusSwitch();
        return true;
    };
    modules_.onEscapeFromSearch = [this] { grabKeyboardFocus(); };

    handle_.setTitle("Resize Sounds column");
    handle_.setTooltip("Drag to resize the Sounds column. Double-click to reset it.");
    handle_.setKeyboardFocusable(true);
    handle_.onDragStarted = [this] {
        widthAnim_.stop();
        widthAtDragStart_ = soundsWidth_;
    };
    handle_.onDragged = [this](int delta) { setSoundsWidth(widthAtDragStart_ + delta, false); };
    handle_.onDragEnded = [this] { commitWidth(); };
    handle_.onResetRequested = [this] { resetSoundsWidth(); };
    handle_.onKeyboardStep = [this](int direction) { setSoundsWidth(soundsWidth_ + direction * kWidthKeyStep, true); };

    installer_->addListener(this);
    folderBook_.addListener(this);
    sounds_.setPackInstaller(&*installer_);
    sounds_.setFolderBook(&folderBook_);
    sounds_.setShapeLibrary(&synth::shapes::ShapeLibrary::shared());
    layoutViews();
}

LibrarySidebarViews::~LibrarySidebarViews() {
    sounds_.setPackInstaller(nullptr); // the shared installer may go with this column
    sounds_.setFolderBook(nullptr);
    sounds_.setShapeLibrary(nullptr);
    folderBook_.removeListener(this);
    installer_->removeListener(this);
    anim_.stop();
    widthAnim_.stop();
    removeChildComponent(&modules_); // the module library outlives this column
    modules_.setWantsKeyboardFocus(modulesTookFocus_);
}

void LibrarySidebarViews::setShortcutHints(const juce::String& modulesKey, const juce::String& soundsKey) {
    switch_.setSegmentTooltips(modulesKey.isEmpty() ? juce::String("Modules") : "Modules (" + modulesKey + ")",
                               "Sounds: search and preview audio" +
                                   (soundsKey.isEmpty() ? juce::String() : " (" + soundsKey + ")"));
}

void LibrarySidebarViews::ensureIndexStarted() {
    if (indexStarted_)
        return;
    indexStarted_ = true;
    folderBook_.goLive();
    index_.rescan();
}

void LibrarySidebarViews::soundFoldersChanged() {
    if (settings_ == nullptr)
        return;
    settings_->setValue(kSoundBrowserFoldersSettingKey, folderBook_.pathsSetting());
    settings_->setValue(kSoundBrowserFolderNamesSettingKey, folderBook_.namesSetting());
    settings_->saveIfNeeded();
}

void LibrarySidebarViews::persistPreviewSettings() {
    if (settings_ == nullptr)
        return;
    settings_->setValue(kSoundBrowserAutoPlaySettingKey, sounds_.isAutoPlay());
    settings_->setValue(kSoundBrowserMatchKeySettingKey, sounds_.isMatchKey());
    settings_->setValue(kSoundBrowserPreviewGainDbSettingKey, (double)sounds_.getPreviewGainDb());
    settings_->saveIfNeeded();
}

void LibrarySidebarViews::persistLists() {
    if (settings_ == nullptr)
        return;
    settings_->setValue(kSoundBrowserFavouritesSettingKey,
                        synth::soundbrowser::SoundLists::toSetting(lists_.favourites()));
    settings_->setValue(kSoundBrowserRecentSettingKey, synth::soundbrowser::SoundLists::toSetting(lists_.recent()));
    settings_->saveIfNeeded();
}

// A download's progress calls this many times; only a change in what is installed needs a new scan.
void LibrarySidebarViews::soundPacksChanged() {
    juce::String signature;
    for (const auto& row : installer_->rows())
        if (row.state == synth::soundpacks::SoundPackInstaller::State::Installed ||
            row.state == synth::soundpacks::SoundPackInstaller::State::UpdateAvailable)
            signature += row.id + ":" + row.installedVersion + ";";
    if (signature == installedSignature_)
        return;
    installedSignature_ = signature;
    if (indexStarted_)
        index_.rescan();
}

void LibrarySidebarViews::visibilityChanged() {
    if (isVisible() && view_ == SidebarView::Sounds)
        ensureIndexStarted();
}

void LibrarySidebarViews::setView(SidebarView view, bool animate) {
    const bool changed = view != view_;
    view_ = view;
    if (settings_ != nullptr) {
        settings_->setValue(kLibrarySidebarViewSettingKey, view == SidebarView::Sounds ? "sounds" : "modules");
        settings_->saveIfNeeded();
    }
    switch_.setView(view, animate);
    if (view == SidebarView::Sounds)
        ensureIndexStarted();
    if (!changed && !mix_.isMoving())
        return;

    const bool tweening = mix_.retarget(view == SidebarView::Sounds ? 1.0f : 0.0f, animate && isShowing());
    if (!tweening) {
        if (vblank_.has_value())
            anim_.stop(*vblank_);
        layoutViews();
        notifyLayout();
        return;
    }
    if (!vblank_.has_value())
        vblank_.emplace(this);
    layoutViews();
    anim_.start(
        *vblank_, motionMs(190.0, 80.0), easeInOutCubic,
        [this](float t) {
            mix_.applyTweenAt(t);
            layoutViews();
            notifyLayout();
        },
        [this] {
            mix_.finish();
            layoutViews();
            notifyLayout();
        });
}

int LibrarySidebarViews::openWidth(int modulesWidth) const {
    if (prefersReducedMotion() && mix_.isMoving())
        return view_ == SidebarView::Sounds ? soundsWidth_ : modulesWidth;
    return mix_.sizeBetween(modulesWidth, soundsWidth_);
}

void LibrarySidebarViews::setSoundsWidth(int width, bool persist) {
    widthAnim_.stop(); // a drag or a key step takes over from a glide in progress
    setSoundsWidthNow(width, persist);
}

void LibrarySidebarViews::setSoundsWidthNow(int width, bool persist) {
    const int clamped = juce::jlimit(kMinSoundsWidth, kMaxSoundsWidth, width);
    if (clamped != soundsWidth_) {
        soundsWidth_ = clamped;
        notifyLayout();
    }
    if (persist)
        commitWidth();
}

// The double-click reset: the width eases from where it is to the default (ease-out, 190 ms) and is saved once, at the
// end. A column that is not on screen gets no frames, so it jumps, as it does under Reduce Motion or Animations Off.
void LibrarySidebarViews::resetSoundsWidth() {
    widthAnim_.stop();
    const int from = soundsWidth_;
    const double ms = motionMs(190.0, 80.0);
    if (from == kDefaultSoundsWidth || prefersReducedMotion() || ms <= 0.0 ||
        !(isShowing() || animateOffscreenForTest_)) {
        setSoundsWidthNow(kDefaultSoundsWidth, true);
        return;
    }
    if (!vblank_.has_value())
        vblank_.emplace(this);
    widthAnim_.start(
        *vblank_, ms, easeOutCubic,
        [this, from](float t) {
            setSoundsWidthNow(from + juce::roundToInt(t * (float)(kDefaultSoundsWidth - from)), false);
        },
        [this] { setSoundsWidthNow(kDefaultSoundsWidth, true); });
}

void LibrarySidebarViews::commitWidth() {
    if (settings_ == nullptr)
        return;
    settings_->setValue(kSoundBrowserWidthSettingKey, soundsWidth_);
    settings_->saveIfNeeded();
}

void LibrarySidebarViews::notifyLayout() {
    if (onLayoutChanged)
        onLayoutChanged();
}

void LibrarySidebarViews::focusShowingViewSearch() {
    if (view_ == SidebarView::Sounds)
        sounds_.focusSearchField();
    else
        modules_.focusSearchField();
}

void LibrarySidebarViews::focusSwitch() { switch_.grabKeyboardFocus(); }

// The root keeps focus (the ring is the whole column's, like the timeline's routing pane); the keys step from here.
void LibrarySidebarViews::focusGained(FocusChangeType) { repaint(); }
void LibrarySidebarViews::focusLost(FocusChangeType) { repaint(); }

void LibrarySidebarViews::paintOverChildren(juce::Graphics& g) { paintFocusRegionOutline(*this, g); }

bool LibrarySidebarViews::keyPressed(const juce::KeyPress& key) {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const bool onRoot = focused == this;
    if (key == juce::KeyPress::escapeKey) {
        if (onRoot)
            return false;
        grabKeyboardFocus();
        return true;
    }
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    const bool down = key.isKeyCode(juce::KeyPress::downKey);
    const bool up = key.isKeyCode(juce::KeyPress::upKey);
    if (down && onRoot) {
        focusSwitch();
        return true;
    }
    if (down && focused == &switch_) {
        focusShowingViewSearch();
        return true;
    }
    if (up && focused == &switch_) {
        grabKeyboardFocus();
        return true;
    }
    if (up && onRoot)
        return true; // nothing above the column's root
    if (onRoot && (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::rightKey))) {
        setView(key.isKeyCode(juce::KeyPress::leftKey) ? SidebarView::Modules : SidebarView::Sounds, true);
        return true;
    }
    return false;
}

void LibrarySidebarViews::paint(juce::Graphics& g) { g.fillAll(synth::theme::themeOf(*this).colors.bg0); }

void LibrarySidebarViews::resized() { layoutViews(); }

// Modules slide out to the left as Sounds slide in from the right; under Reduce Motion both stay put and cross-fade.
void LibrarySidebarViews::layoutViews() {
    auto bounds = getLocalBounds();
    switch_.setBounds(bounds.withHeight(LibrarySidebarSwitch::kHeight));
    const auto content = bounds.withTrimmedTop(LibrarySidebarSwitch::kHeight);
    const float mix = mix_.getProgress();
    const int w = content.getWidth();
    const bool fade = prefersReducedMotion();
    const int modulesX = fade ? 0 : -juce::roundToInt(mix * (float)w);
    const int soundsX = fade ? 0 : juce::roundToInt((1.0f - mix) * (float)w);
    modules_.setBounds(content.withX(modulesX));
    sounds_.setBounds(content.withX(soundsX));
    modules_.setAlpha(fade ? 1.0f - mix : 1.0f);
    sounds_.setAlpha(fade ? mix : 1.0f);
    modules_.setVisible(mix < 1.0f);
    sounds_.setVisible(mix > 0.0f);
    handle_.setVisible(view_ == SidebarView::Sounds);
    handle_.setBounds(getLocalBounds().removeFromRight(6));
    handle_.toFront(false);
}

} // namespace synth::ui
