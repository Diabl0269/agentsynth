#include "UI/Settings/SoundPacks/SoundPacksSettingsTab.h"
#include "UI/Settings/SettingsFoldState.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace {

constexpr int kPad = 10;
constexpr int kTitleH = 24;
constexpr int kIntroH = 34;
constexpr int kStatusH = 20;
constexpr int kRowGap = 6;
constexpr int kSectionGap = 12;
constexpr int kHeadingH = 22;
constexpr const char* kOpenPacksKey = "soundPacksOpen";
constexpr int kScrollbarW = 14;
constexpr int kRetryW = 50;

using Installer = synth::soundpacks::SoundPackInstaller;

void askWithAlert(const juce::String& message, std::function<void()> onYes) {
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Remove sound pack")
                             .withMessage(message)
                             .withButton("Remove")
                             .withButton("Cancel");
    juce::AlertWindow::showAsync(options, [onYes = std::move(onYes)](int result) {
        if (result == 1 && onYes)
            onYes();
    });
}

} // namespace

SoundPacksSettingsTab::SoundPacksSettingsTab(Installer& installer, synth::soundbrowser::SoundFolderBook* folders,
                                             juce::ApplicationProperties* props)
    : installer_(installer)
    , folders_(folders)
    , props_(props)
    , openPacks_(synth::ui::settingsfold::load(props, kOpenPacksKey))
    , confirmer_(askWithAlert) {
    setTitle("Sound packs");

    addAndMakeVisible(title_);
    title_.setText("Sound packs", juce::dontSendNotification);
    title_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    addAndMakeVisible(intro_);
    intro_.setText("Optional instruments for the Sampler. They download to this device when you ask for them. "
                   "Open a pack to see who made it, its licence and every instrument.",
                   juce::dontSendNotification);
    intro_.setJustificationType(juce::Justification::topLeft);
    intro_.setMinimumHorizontalScale(1.0f);

    addAndMakeVisible(status_);
    status_.setMinimumHorizontalScale(1.0f);
    addChildComponent(retry_);
    retry_.setTitle("Retry loading the sound pack list");
    retry_.setTooltip("Try to reach the list of sound packs again");
    retry_.onClick = [this] { installer_.refreshIndex(); };

    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setWantsKeyboardFocus(false);

    if (folders_ != nullptr) {
        content_.addAndMakeVisible(foldersHeading_);
        foldersHeading_.setText("Your folders", juce::dontSendNotification);
        foldersHeading_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        content_.addAndMakeVisible(foldersEmpty_);
        foldersEmpty_.setText("None yet. Add one with Add folder... under Places in the Sounds view.",
                              juce::dontSendNotification);
        foldersEmpty_.setMinimumHorizontalScale(1.0f);
        folders_->addListener(this);
    }

    lookAndFeelChanged();
    installer_.addListener(this);
    refresh();
    rebuildFolderRows();
}

SoundPacksSettingsTab::~SoundPacksSettingsTab() {
    if (folders_ != nullptr)
        folders_->removeListener(this);
    installer_.removeListener(this);
}

void SoundPacksSettingsTab::paint(juce::Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}

void SoundPacksSettingsTab::lookAndFeelChanged() {
    status_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
    foldersEmpty_.setColour(juce::Label::textColourId, synth::theme::themeOf(*this).colors.textMuted);
}

void SoundPacksSettingsTab::visibilityChanged() {
    if (isShowing())
        installer_.refreshIndex();
}

void SoundPacksSettingsTab::resized() {
    auto bounds = getLocalBounds().reduced(kPad);
    title_.setBounds(bounds.removeFromTop(kTitleH));
    bounds.removeFromTop(2);
    intro_.setBounds(bounds.removeFromTop(kIntroH));
    bounds.removeFromTop(4);
    auto statusRow = bounds.removeFromTop(kStatusH);
    retry_.setBounds(statusRow.removeFromRight(kRetryW));
    status_.setBounds(statusRow);
    bounds.removeFromTop(4);
    viewport_.setBounds(bounds);
    layoutContent();
}

void SoundPacksSettingsTab::soundPacksChanged() { refresh(); }

// The change may come from a row's own Remove button, so the rows are replaced once that click has returned.
void SoundPacksSettingsTab::soundFoldersChanged() {
    juce::Component::SafePointer<SoundPacksSettingsTab> safe(this);
    juce::MessageManager::callAsync([safe] {
        if (safe != nullptr)
            safe->rebuildFolderRows();
    });
}

// The folder rows are cheap and few, so any change to the list rebuilds all of them.
void SoundPacksSettingsTab::rebuildFolderRows() {
    if (folders_ == nullptr)
        return;
    folderRows_.clear();
    for (const auto& folder : folders_->folders()) {
        folderRows_.push_back(std::make_unique<synth::ui::SoundFolderRow>(
            folder, [this](const juce::File& path) { folders_->remove(path); }));
        content_.addAndMakeVisible(*folderRows_.back());
    }
    foldersEmpty_.setVisible(folderRows_.empty());
    layoutContent();
}

void SoundPacksSettingsTab::refresh() {
    using Status = Installer::IndexStatus;
    const auto rows = installer_.rows();
    juce::String line;
    switch (installer_.indexStatus()) {
    case Status::Loading:
        line = "Checking for sound packs...";
        break;
    case Status::Failed:
        line = installer_.indexError();
        break;
    case Status::Loaded:
        if (rows.empty())
            line = "No sound packs are listed yet.";
        break;
    case Status::NotLoaded:
        break;
    }
    status_.setText(line, juce::dontSendNotification);
    retryFade_.setShown(installer_.indexStatus() == Status::Failed);
    rebuildComponents(rows);
}

// The row set changes when the list loads or a pack the list does not know is removed; then the rows are rebuilt and
// the whole list fades in. Otherwise each row just takes its new state.
void SoundPacksSettingsTab::rebuildComponents(const std::vector<Installer::Row>& rows) {
    juce::StringArray ids;
    for (const auto& row : rows)
        ids.add(row.id);
    const bool changed = ids != shownIds_;
    if (changed) {
        shownIds_ = ids;
        rows_.clear();
        for (const auto& row : rows) {
            rows_.push_back(std::make_unique<synth::ui::SoundPackRow>(
                row.id,
                synth::ui::SoundPackRow::Actions{[this](const juce::String& id) { installer_.install(id); },
                                                 [this](const juce::String& id) { installer_.cancel(id); },
                                                 [this](const juce::String& id) { askToRemove(id); },
                                                 [this](const juce::String& url) { urlOpener_(juce::URL(url)); },
                                                 [this](const juce::String& id) { installer_.showSources(id); }}));
            auto& made = *rows_.back();
            content_.addAndMakeVisible(made);
            made.setExpanded(openPacks_.contains(row.id), false); // a remembered row is there from the start
            made.onHeightChanged = [this] { layoutContent(); };   // each frame of a row's glide
            made.onExpandedChanged = [this, id = row.id] {
                const auto it =
                    std::find_if(rows_.begin(), rows_.end(), [&id](const auto& r) { return r->getPackId() == id; });
                if (it != rows_.end()) {
                    openPacks_.removeString(id);
                    if ((*it)->isExpanded())
                        openPacks_.add(id);
                }
                saveOpenPacks();
                layoutContent();
            };
        }
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        rows_[i]->update(rows[i]);
    }
    layoutContent();
    if (changed) {
        listFade_.snapTo(false);
        listFade_.setShown(!rows.empty());
    }
}

void SoundPacksSettingsTab::layoutContent() {
    const int width = juce::jmax(1, viewport_.getWidth() - kScrollbarW);
    // A row that ends above the viewport's top and changes height moves what the user is reading; the view
    // follows by the same amount so the glide never shifts it.
    const int viewTop = viewport_.getViewPositionY();
    int scrollShift = 0;
    int y = 0;
    for (auto& row : rows_) {
        const int height = row->heightForWidth(width);
        if (row->getBottom() <= viewTop && row->getHeight() > 0)
            scrollShift += height - row->getHeight();
        row->setBounds(0, y, width, height);
        y += height + kRowGap;
    }
    y += kSectionGap;
    if (folders_ != nullptr) {
        foldersHeading_.setBounds(0, y, width, kHeadingH);
        y += kHeadingH + kRowGap;
        if (folderRows_.empty()) {
            foldersEmpty_.setBounds(0, y, width, kHeadingH);
            y += kHeadingH + kRowGap;
        }
        for (auto& row : folderRows_) {
            row->setBounds(0, y, width, synth::ui::SoundFolderRow::kHeight);
            y += synth::ui::SoundFolderRow::kHeight + kRowGap;
        }
        y += kSectionGap;
    }
    content_.setSize(width, juce::jmax(y, 1));
    if (scrollShift != 0)
        viewport_.setViewPosition(viewport_.getViewPositionX(), juce::jmax(0, viewTop + scrollShift));
}

void SoundPacksSettingsTab::saveOpenPacks() { synth::ui::settingsfold::save(props_, kOpenPacksKey, openPacks_); }

void SoundPacksSettingsTab::askToRemove(const juce::String& packId) {
    const auto row = installer_.row(packId);
    if (!row)
        return;
    juce::Component::SafePointer<SoundPacksSettingsTab> safe(this);
    confirmer_("Remove " + row->name +
                   " from this device? You can download it again later. Projects that use its "
                   "instruments will not find them until it is installed again.",
               [safe, packId] {
                   if (safe != nullptr)
                       safe->installer_.remove(packId);
               });
}
