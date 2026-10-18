#pragma once

// The content of the instrument map window: the keyboard and rows of recordings, the selected recording's fields and,
// for a built-in instrument, the notice that the first edit makes a copy. Every edit goes through `applyEdit` (one
// undo step each) and rebuilds the instrument; the view follows the module, so an undo shows at once.

#include "Modules/SamplerModule.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapBlocks.h"
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapFields.h"
#include "UI/Layout/ArrowKeyNavigation.h"
#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class SamplerMapView : public juce::Component {
public:
    using ApplyEdit = std::function<bool(const std::vector<synth::sampler::RegionSpec>&)>;

    static constexpr int kWidth = 780;
    static constexpr int kHeight = 470;

    /** `sampler` must outlive this or be released with detach(). `applyEdit` makes the edit (and its undo step) and
     *  returns whether the instrument was replaced. */
    SamplerMapView(SamplerModule& sampler, ApplyEdit applyEdit);
    ~SamplerMapView() override;

    /** Lets go of the module; the view then shows nothing and edits nothing. */
    void detach();
    /** Follows the module: rebuilds when the instrument changed (an undo, a load). Cheap when nothing did. */
    void poll();

    /** True for a built-in (sound pack) instrument until its first edit makes the module's own copy. */
    bool isReadOnly() const noexcept { return readOnly_; }
    juce::String getWindowTitle() const;
    int getSelected() const noexcept { return blocks_.getSelected(); }

    // ---- The edits (what the controls call) ----
    /** Hands `specs` to applyEdit and shows the result with region `selectAfter` selected. */
    bool commit(const std::vector<synth::sampler::RegionSpec>& specs, int selectAfter);
    /** Makes the module's own copy of a built-in instrument without changing anything in it. */
    bool makeOwnCopy();
    bool replaceFile(int region, const juce::String& path);
    bool addRecordings(const juce::StringArray& paths, int key, int loVel, int hiVel);
    bool removeSelected();

    // ---- Parts, for tests ----
    SamplerMapBlocks& getBlocks() noexcept { return blocks_; }
    SamplerMapFields& getFields() noexcept { return fields_; }
    juce::TextButton& getCopyButton() noexcept { return copyButton_; }
    juce::TextButton& getAddButton() noexcept { return addButton_; }
    juce::TextButton& getDoneButton() noexcept { return doneButton_; }
    juce::Label& getNotice() noexcept { return notice_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void refresh(int selected);
    void applyFieldChange(const synth::sampler::RegionSpec& region);
    void launchRecordingChooser(bool replace);
    void applyReadOnly();
    std::vector<synth::sampler::RegionSpec> regions() const;

    SamplerModule* sampler_;
    ApplyEdit applyEdit_;
    SamplerMapBlocks blocks_;
    SamplerMapFields fields_;
    juce::Label notice_{"Notice", "This is a built-in instrument. Making your own copy lets you change it."};
    juce::TextButton copyButton_{"Make my own copy"};
    juce::TextButton addButton_{"Add recording..."};
    juce::TextButton doneButton_{"Done"};
    FadeVisibility noticeFade_{&notice_, &copyButton_};
    std::unique_ptr<ArrowKeyNavigation> fieldKeys_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int shownGeneration_ = -1;
    bool readOnly_ = false;
    bool committing_ = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE(SamplerMapView)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamplerMapView)
};

} // namespace synth::ui
