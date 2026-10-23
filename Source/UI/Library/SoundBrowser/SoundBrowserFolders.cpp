// SoundBrowserFolders.cpp -- the user's own folders in the Sounds view: the "Add folder..." row and its chooser,
// folders dropped from Finder, the right-click (and Shift+F10) menu of a folder Place, renaming it in place, and
// noticing that a drive came or went.
#include "UI/Layout/ContextMenuPlacement.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include "UI/Library/SoundBrowser/SoundBrowserPackLayout.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kFolderPrefixLength = 7; // "folder:"
constexpr float kDropOutlineWidth = 2.0f;

juce::File folderOfPlace(const juce::String& placeId) { return juce::File(placeId.substring(kFolderPrefixLength)); }

bool allFolders(const juce::StringArray& files) {
    if (files.isEmpty())
        return false;
    for (const auto& f : files)
        if (!juce::File(f).isDirectory())
            return false;
    return true;
}

} // namespace

void SoundBrowserComponent::setupFolders() {
    renameLabel_.setTitle("Folder name");
    renameLabel_.setTooltip("Type a new name for this folder. It changes only the name shown here.");
    renameLabel_.setFont(juce::Font(juce::FontOptions(14.0f)));
    renameLabel_.setEditable(false, false, false);
    renameLabel_.setMinimumHorizontalScale(1.0f);
    renameLabel_.onTextChange = [this] { commitRename(); };
    renameLabel_.onEditorHide = [this] { endRename(); };
    addChildComponent(renameLabel_);
    foregroundCheck_ = [] { return juce::Process::isForegroundProcess(); };
}

void SoundBrowserComponent::setFolderBook(synth::soundbrowser::SoundFolderBook* book) {
    if (folderBook_ != nullptr)
        folderBook_->removeListener(this);
    folderBook_ = book;
    if (folderBook_ != nullptr)
        folderBook_->addListener(this);
    rebuildRows();
}

void SoundBrowserComponent::soundFoldersChanged() {
    if (placeId_.startsWith("folder:") &&
        (folderBook_ == nullptr || folderBook_->find(folderOfPlace(placeId_)) == nullptr))
        navigateTo({}, {}, -1, {});
    else
        rebuildRows();
}

// ---- Adding ----

void SoundBrowserComponent::chooseFolderToAdd() {
    if (folderBook_ == nullptr)
        return;
    juce::Component::SafePointer<SoundBrowserComponent> safe(this);
    const auto picked = [safe](const juce::File& folder) {
        if (safe != nullptr && folder != juce::File())
            safe->addFolders(juce::Array<juce::File>{folder});
    };
    if (folderPicker_) {
        folderPicker_(picked);
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>("Choose a folder of sounds",
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [picked](const juce::FileChooser& fc) { picked(fc.getResult()); });
}

bool SoundBrowserComponent::addFolders(const juce::Array<juce::File>& folders) {
    if (folderBook_ == nullptr)
        return false;
    juce::File last;
    for (const auto& folder : folders)
        if (folderBook_->add(folder))
            last = folder;
    if (last == juce::File())
        return false;
    pendingSelection_ = "p:folder:" + last.getFullPathName();
    if (!placeId_.isEmpty() || isSearchActive())
        navigateTo({}, {}, -1, pendingSelection_);
    return true;
}

bool SoundBrowserComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return folderBook_ != nullptr && allFolders(files);
}

void SoundBrowserComponent::filesDropped(const juce::StringArray& files, int, int) {
    setDropActive(false);
    juce::Array<juce::File> folders;
    for (const auto& f : files)
        folders.add(juce::File(f));
    addFolders(folders);
}

void SoundBrowserComponent::fileDragEnter(const juce::StringArray&, int, int) { setDropActive(true); }

void SoundBrowserComponent::fileDragExit(const juce::StringArray&) { setDropActive(false); }

// The accent outline around the list fades in while a folder from Finder is over the view and out when it leaves.
void SoundBrowserComponent::setDropActive(bool active) {
    if (dropActive_ == active)
        return;
    dropActive_ = active;
    if (!vblank_.has_value())
        vblank_.emplace(this);
    const float from = dropGlow_;
    const float to = active ? 1.0f : 0.0f;
    dropAnim_.start(
        *vblank_, motionMs(120.0, 80.0), easeOutCubic,
        [this, from, to](float t) {
            dropGlow_ = from + (to - from) * t;
            repaint(listArea());
        },
        [this, to] {
            dropGlow_ = to;
            repaint(listArea());
        });
}

void SoundBrowserComponent::paintDropOutline(juce::Graphics& g) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    g.setColour(c.accent.withAlpha(0.9f * dropGlow_));
    g.drawRect(listArea().toFloat().reduced(kDropOutlineWidth / 2.0f), kDropOutlineWidth);
}

void SoundBrowserComponent::paintAddFolderRow(juce::Graphics& g, juce::Rectangle<int> area) const {
    const auto& c = synth::theme::themeOf(*this).colors;
    const float cx = (float)area.getX() + (float)kSoundRowSideMargin + 5.0f;
    const float cy = (float)area.getCentreY();
    g.setColour(c.accent);
    g.drawLine(cx - 4.5f, cy, cx + 4.5f, cy, 1.5f);
    g.drawLine(cx, cy - 4.5f, cx, cy + 4.5f, 1.5f);
    g.setColour(c.textPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText("Add folder...", area.getX() + kSoundRowSideMargin + 18, area.getY(),
               juce::jmax(0, area.getWidth() - 2 * kSoundRowSideMargin - 18), area.getHeight(),
               juce::Justification::centredLeft, true);
}

// ---- The folder menu ----

juce::PopupMenu SoundBrowserComponent::buildFolderMenu(int rowIndex) const {
    juce::PopupMenu menu;
    if (rowIndex < 0 || rowIndex >= (int)rows_.size() || !rows_[(size_t)rowIndex].userFolder)
        return menu;
    menu.addItem(1, "Rename");
    menu.addItem(2, "Show in Finder", !rows_[(size_t)rowIndex].missing);
    menu.addItem(3, "Scan again");
    menu.addSeparator();
    menu.addItem(4, "Remove");
    return menu;
}

void SoundBrowserComponent::showFolderMenu(int rowIndex, std::optional<juce::Rectangle<int>> anchor) {
    auto menu = buildFolderMenu(rowIndex);
    if (menu.getNumItems() == 0)
        return;
    const auto placeId = rows_[(size_t)rowIndex].placeId;
    juce::Component::SafePointer<SoundBrowserComponent> safe(this);
    menu.showMenuAsync(contextMenuOptions(anchor), [safe, placeId](int result) {
        if (safe == nullptr || result < 1 || result > 4)
            return;
        for (int i = 0; i < (int)safe->rows_.size(); ++i)
            if (safe->rows_[(size_t)i].userFolder && safe->rows_[(size_t)i].placeId == placeId) {
                safe->applyFolderAction(i, (FolderAction)(result - 1));
                return;
            }
    });
}

bool SoundBrowserComponent::showContextMenuForKeyboardFocus() {
    if (selected_ < 0 || selected_ >= (int)rows_.size() || !rows_[(size_t)selected_].userFolder)
        return false;
    showFolderMenu(selected_, localAreaToGlobal(getRowBounds(selected_)));
    return true;
}

bool SoundBrowserComponent::applyFolderAction(int rowIndex, FolderAction action) {
    if (folderBook_ == nullptr || rowIndex < 0 || rowIndex >= (int)rows_.size() || !rows_[(size_t)rowIndex].userFolder)
        return false;
    const auto placeId = rows_[(size_t)rowIndex].placeId;
    const auto folder = folderOfPlace(placeId);
    switch (action) {
    case FolderAction::Rename:
        return beginRename(rowIndex);
    case FolderAction::ShowInFinder:
        if (reveal_)
            reveal_(folder);
        else
            folder.revealToUser();
        return true;
    case FolderAction::ScanAgain:
        index_.rescan();
        return true;
    case FolderAction::Remove:
        folderBook_->remove(folder);
        return true;
    }
    return false;
}

// ---- Renaming ----

bool SoundBrowserComponent::beginRename(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= (int)rows_.size() || !rows_[(size_t)rowIndex].userFolder)
        return false;
    const auto& row = rows_[(size_t)rowIndex];
    renaming_ = folderOfPlace(row.placeId);
    scrollSelectionIntoView();
    const auto bounds = getRowBounds(rowIndex);
    renameLabel_.setBounds(bounds.getX() + kSoundRowSideMargin - 4, bounds.getY() + 3,
                           juce::jmax(40, bounds.getWidth() - 2 * kSoundRowSideMargin - 30), bounds.getHeight() - 6);
    renameLabel_.setText(row.text, juce::dontSendNotification);
    renameLabel_.setVisible(true);
    renameLabel_.showEditor();
    return true;
}

// Only the displayed name changes: the folder on disk, its files and its place in the index keep their paths.
void SoundBrowserComponent::commitRename() {
    if (folderBook_ != nullptr && renaming_ != juce::File())
        folderBook_->rename(renaming_, renameLabel_.getText());
}

// `renaming_` stays set: a commit from a focus loss can arrive after the editor has already been hidden.
void SoundBrowserComponent::endRename() {
    renameLabel_.setVisible(false);
    if (!tearingDown_)
        rebuildRows();
}

// ---- Drives coming and going ----

// Focus moves all the time; only the edge from another app to this one means a drive may have changed.
void SoundBrowserComponent::noteAppFocus() {
    const bool foreground = foregroundCheck_ ? foregroundCheck_() : true;
    if (foreground && !appWasForeground_)
        checkFoldersOnReturn();
    appWasForeground_ = foreground;
}

void SoundBrowserComponent::checkFoldersOnReturn() {
    if (folderBook_ != nullptr)
        index_.rescanIfFoldersChanged();
}

} // namespace synth::ui
