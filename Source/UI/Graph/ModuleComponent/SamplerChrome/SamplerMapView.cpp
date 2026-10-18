// SamplerMapView.cpp -- the instrument map window's content: how a control or a drop becomes an edit of the
// instrument's regions, and how the view follows the module.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapView.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {

constexpr int kMargin = 14;
constexpr int kGap = 10;
constexpr int kNoticeHeight = 30;
constexpr int kFieldsHeight = 124; // the file row, a gap and two rows of captioned fields
constexpr int kBottomBarHeight = 30;
constexpr int kButtonWidth = 130;
constexpr int kDoneWidth = 90;

void styleButton(juce::TextButton& button, const juce::String& tooltip) {
    button.setTitle(button.getButtonText());
    button.setTooltip(tooltip);
}

} // namespace

SamplerMapView::SamplerMapView(SamplerModule& sampler, ApplyEdit applyEdit)
    : sampler_(&sampler)
    , applyEdit_(std::move(applyEdit)) {
    setWantsKeyboardFocus(false);
    setTitle("Instrument map");
    addAndMakeVisible(blocks_);
    addAndMakeVisible(fields_);
    for (juce::Component* c : {(juce::Component*)&notice_, (juce::Component*)&copyButton_,
                               (juce::Component*)&addButton_, (juce::Component*)&doneButton_})
        addAndMakeVisible(*c);
    notice_.setAccessible(false);
    styleButton(copyButton_, "Make your own copy of this built-in instrument so its recordings can be changed");
    styleButton(addButton_, "Add one or more recordings after the selected one");
    styleButton(doneButton_, "Close the map (Esc)");
    blocks_.setExplicitFocusOrder(1);
    fields_.setExplicitFocusOrder(2);
    copyButton_.setExplicitFocusOrder(3);
    addButton_.setExplicitFocusOrder(4);
    doneButton_.setExplicitFocusOrder(5);

    blocks_.onSelect = [this](int index) {
        const auto all = regions();
        fields_.showRegion(index >= 0 && index < (int)all.size() ? &all[(size_t)index] : nullptr);
    };
    blocks_.onOpenFields = [this] { fields_.focusFirstField(); };
    blocks_.onRemove = [this] {
        if (!readOnly_)
            removeSelected();
    };
    blocks_.onDrop = [this](const MapDrop& drop) {
        if (drop.replaceRegion >= 0)
            replaceFile(drop.replaceRegion, drop.paths[0]);
        else
            addRecordings(drop.paths, drop.key, drop.loVel, drop.hiVel);
    };
    fields_.onChange = [this](const synth::sampler::RegionSpec& r) { applyFieldChange(r); };
    fields_.onReplaceFile = [this] { launchRecordingChooser(true); };
    fields_.onRemove = [this] { removeSelected(); };
    copyButton_.onClick = [this] { makeOwnCopy(); };
    addButton_.onClick = [this] { launchRecordingChooser(false); };
    doneButton_.onClick = [this] { closeHostingWindow(*this); };

    fieldKeys_ = std::make_unique<ArrowKeyNavigation>(fields_);
    refresh(-1);
    noticeFade_.snapTo(readOnly_);
}

SamplerMapView::~SamplerMapView() = default;

void SamplerMapView::detach() {
    sampler_ = nullptr;
    applyEdit_ = nullptr;
    blocks_.setRegions({}, -1);
    fields_.showRegion(nullptr);
    blocks_.setEditable(false);
    fields_.setEditable(false);
}

std::vector<synth::sampler::RegionSpec> SamplerMapView::regions() const {
    return sampler_ != nullptr ? sampler_->getInstrumentRegions() : std::vector<synth::sampler::RegionSpec>{};
}

juce::String SamplerMapView::getWindowTitle() const {
    const juce::String name = sampler_ != nullptr ? sampler_->getInstrumentName() : juce::String();
    return (name.isEmpty() ? juce::String("Instrument") : name) + " - map";
}

void SamplerMapView::applyReadOnly() {
    const bool pack =
        sampler_ != nullptr && sampler_->getInstrumentKind() == synth::sampler::InstrumentSource::Kind::Pack;
    readOnly_ = pack;
    blocks_.setEditable(!pack && sampler_ != nullptr);
    fields_.setEditable(!pack && sampler_ != nullptr);
    addButton_.setEnabled(!pack && sampler_ != nullptr);
    noticeFade_.setShown(pack);
}

// Shows the module's list with `selected` selected (nothing when the list no longer has it).
void SamplerMapView::refresh(int selected) {
    if (sampler_ == nullptr)
        return;
    shownGeneration_ = sampler_->getInstrumentGeneration();
    const auto all = regions();
    blocks_.setRegions(all, selected);
    fields_.showRegion(blocks_.getSelected() >= 0 ? &all[(size_t)blocks_.getSelected()] : nullptr);
    applyReadOnly();
}

void SamplerMapView::poll() {
    if (sampler_ != nullptr && !committing_ && sampler_->getInstrumentGeneration() != shownGeneration_)
        refresh(blocks_.getSelected());
}

bool SamplerMapView::commit(const std::vector<synth::sampler::RegionSpec>& specs, int selectAfter) {
    if (sampler_ == nullptr || !applyEdit_)
        return false;
    committing_ = true;
    const bool ok = applyEdit_(specs);
    committing_ = false;
    refresh(ok ? selectAfter : blocks_.getSelected());
    return ok;
}

bool SamplerMapView::makeOwnCopy() { return readOnly_ && commit(regions(), blocks_.getSelected()); }

bool SamplerMapView::replaceFile(int region, const juce::String& path) {
    if (readOnly_)
        return false;
    return commit(synth::sampler::replaceRecording(regions(), region, path), region);
}

bool SamplerMapView::addRecordings(const juce::StringArray& paths, int key, int loVel, int hiVel) {
    if (readOnly_ || paths.isEmpty())
        return false;
    auto specs = regions();
    for (int i = 0; i < paths.size(); ++i)
        specs = synth::sampler::addRecording(std::move(specs), paths[i], juce::jmin(127, key + i), loVel, hiVel);
    return commit(specs, (int)specs.size() - 1);
}

bool SamplerMapView::removeSelected() {
    const int selected = blocks_.getSelected();
    if (readOnly_ || selected < 0)
        return false;
    const auto all = regions();
    if (all.size() < 2)
        return false;
    return commit(synth::sampler::removeRegion(all, selected), juce::jmin(selected, (int)all.size() - 2));
}

// A field edit is applied from the panel, which already shows the values: only the blocks follow.
void SamplerMapView::applyFieldChange(const synth::sampler::RegionSpec& region) {
    const int selected = blocks_.getSelected();
    auto all = regions();
    if (sampler_ == nullptr || readOnly_ || selected < 0 || selected >= (int)all.size())
        return;
    all[(size_t)selected] = region;
    committing_ = true;
    const bool ok = applyEdit_ && applyEdit_(all);
    committing_ = false;
    const auto now = regions();
    blocks_.setRegions(now, selected);
    shownGeneration_ = sampler_->getInstrumentGeneration();
    if (!ok)
        fields_.showRegion(&now[(size_t)selected]);
    applyReadOnly();
}

void SamplerMapView::launchRecordingChooser(bool replace) {
    const int selected = blocks_.getSelected();
    if (readOnly_ || (replace && selected < 0))
        return;
    chooser_ = std::make_unique<juce::FileChooser>(replace ? "Replace recording" : "Add recordings",
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                   SamplerModule::getSupportedFormatWildcard());
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    if (!replace)
        flags |= juce::FileBrowserComponent::canSelectMultipleItems;
    juce::WeakReference<SamplerMapView> weak(this);
    chooser_->launchAsync(flags, [weak, replace, selected](const juce::FileChooser& fc) {
        if (weak == nullptr || fc.getResults().isEmpty())
            return;
        if (replace) {
            weak->replaceFile(selected, fc.getResult().getFullPathName());
            return;
        }
        juce::StringArray paths;
        for (const auto& file : fc.getResults())
            paths.add(file.getFullPathName());
        const auto all = weak->regions();
        const int after = weak->blocks_.getSelected();
        const int key = after >= 0 ? juce::jmin(127, all[(size_t)after].hiKey + 1) : 60;
        const int lo = after >= 0 ? all[(size_t)after].loVel : 1;
        const int hi = after >= 0 ? all[(size_t)after].hiVel : 127;
        weak->addRecordings(paths, key, lo, hi);
    });
}

// The dialog window paints nothing of its own, so the view fills it with the theme surface like the shape editor.
void SamplerMapView::paint(juce::Graphics& g) { g.fillAll(synth::theme::themeOf(*this).colors.surface); }

void SamplerMapView::resized() {
    auto area = getLocalBounds().reduced(kMargin);
    auto bottom = area.removeFromBottom(kBottomBarHeight);
    doneButton_.setBounds(bottom.removeFromRight(kDoneWidth));
    bottom.removeFromRight(kGap);
    addButton_.setBounds(bottom.removeFromRight(kButtonWidth));
    area.removeFromBottom(kGap);
    fields_.setBounds(area.removeFromBottom(kFieldsHeight));
    area.removeFromBottom(kGap);
    auto notice = area.removeFromTop(kNoticeHeight);
    copyButton_.setBounds(notice.removeFromRight(kButtonWidth + 20));
    notice_.setBounds(notice);
    area.removeFromTop(kGap);
    blocks_.setBounds(area);
}

bool SamplerMapView::keyPressed(const juce::KeyPress& key) {
    return key == juce::KeyPress::escapeKey && closeHostingWindow(*this);
}

} // namespace synth::ui
