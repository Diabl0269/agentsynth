// TimelineSectionStripAccessibility.cpp
//
// What the Sections strip says to a screen reader and a hovering pointer: the strip as a named group, one invisible
// proxy child per block (the MixerInsertList::RowAccessibilityProxy pattern), the tooltips, and the test-mode probe a
// ui-verify scenario reads.

#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

#include "ShortcutManager/ShortcutManager.h"
#include "TestMode/TestModeStateSource.h"
#include "Timeline/TimelineDoc/SectionVariationLetter.h"
#include <algorithm>
#include <map>

namespace synth::ui {

namespace {
constexpr int kMaxProxyCoordinate = 1'000'000;
} // namespace

// A child of the strip that only answers test-mode getState, so the strip's own header stays out of
// TestModeStateSource.h. `sections` lists every placement as "name:startBeat:lengthBeats:ordinal/count" joined by ";"
// in placement order; `selectedSection` is the selected placement's start beat, or empty; `variations` is the letter
// each placement plays in the same order, with a "*" after it when its section has more than one variation
// (`A*;B*;A*`); `sectionVariationCount` is how many variations the selected placement's section has (0 with nothing
// selected).
class TimelineSectionStrip::Probe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit Probe(std::function<void(juce::DynamicObject&)> fill)
        : fill_(std::move(fill)) {
        setComponentID("timelineSections");
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override { fill_(into); }

private:
    std::function<void(juce::DynamicObject&)> fill_;
};

std::unique_ptr<juce::AccessibilityHandler> TimelineSectionStrip::BlockProxy::createAccessibilityHandler() {
    // "Show menu" reaches the variation menu; the press (rename) stays the default action.
    juce::AccessibilityActions actions;
    actions.addAction(juce::AccessibilityActionType::showMenu, [this] {
        if (showVariations)
            showVariations();
    });
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button, std::move(actions));
}

std::unique_ptr<juce::AccessibilityHandler> TimelineSectionStrip::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
}

// The constructor and destructor live here: Probe is only complete in this file.
TimelineSectionStrip::TimelineSectionStrip(TimelineViewState& viewState)
    : viewState_(viewState) {
    setComponentID("timelineSectionStrip");
    setTitle("Sections");
    setDescription("Sections, 0 sections");
    setWantsKeyboardFocus(true); // a click focuses it (JUCE's default), Tab reaches it; its keys are in Keys.cpp
    addTestProbe();
}

TimelineSectionStrip::~TimelineSectionStrip() = default;

void TimelineSectionStrip::addTestProbe() {
    probe_ = std::make_unique<Probe>([this](juce::DynamicObject& into) {
        juce::StringArray sections;
        juce::StringArray variations;
        juce::String selected;
        int selectedVariationCount = 0;
        if (doc_ != nullptr) {
            std::map<std::int64_t, int> total;
            for (const auto& placement : doc_->getPlacements())
                ++total[placement.section.value];
            std::map<std::int64_t, int> seen;
            for (const auto& placement : doc_->getPlacements()) {
                const auto* section = doc_->getSection(placement.section);
                if (section == nullptr)
                    continue;
                sections.add(section->name + ":" + juce::String(placement.startBeat, 3) + ":" +
                             juce::String(section->lengthBeats, 3) + ":" +
                             juce::String(++seen[placement.section.value]) + "/" +
                             juce::String(total[placement.section.value]));
                const bool several = section->variations.size() > 1;
                variations.add(synth::variationLetter(placement.variation) + (several ? "*" : ""));
                if (placement.id == selected_) {
                    selected = juce::String(placement.startBeat, 3);
                    selectedVariationCount = (int)section->variations.size();
                }
            }
        }
        into.setProperty("sections", sections.joinIntoString(";"));
        into.setProperty("selectedSection", selected);
        into.setProperty("variations", variations.joinIntoString(";"));
        into.setProperty("sectionVariationCount", selectedVariationCount);
    });
    probe_->setBounds(0, 0, 1, 1);
    addAndMakeVisible(*probe_);
}

//==============================================================================
juce::String TimelineSectionStrip::tooltipAt(juce::Point<float> pos) const {
    if (doc_ != nullptr) {
        for (const auto& block : buildBlocks()) {
            if (!block.bounds.contains(pos))
                continue;
            const auto letter = synth::variationLetter(block.variation);
            if (sectionstrip::chipBounds(block.bounds).contains(pos))
                return sectionstrip::chipTooltip(letter, block.name);
            return sectionstrip::blockTooltip(block.name, block.lengthBeats, beatsPerBar(), block.count,
                                              platformCommandKeyName(),
                                              block.variationCount > 1 ? letter : juce::String());
        }
    }
    const juce::KeyPress fallback('j', juce::ModifierKeys::shiftModifier | juce::ModifierKeys::commandModifier, 0);
    const auto hint = shortcutHintFor(shortcuts_, "timelineMakeSection", fallback);
    return "Drag across bars to make a section" + (hint.isEmpty() ? juce::String() : " (" + hint + ")");
}

juce::String TimelineSectionStrip::getTooltip() { return tooltipAt(getMouseXYRelative().toFloat()); }

//==============================================================================
// One proxy per placement (visible or not: a screen reader walks them all), rebuilt only when what they say changed.
void TimelineSectionStrip::rebuildProxies() {
    std::vector<std::pair<juce::String, juce::String>> text;
    int sectionCount = 0;
    if (doc_ != nullptr) {
        sectionCount = (int)doc_->getSections().size();
        std::map<std::int64_t, int> total;
        for (const auto& placement : doc_->getPlacements())
            ++total[placement.section.value];
        std::map<std::int64_t, int> seen;
        const double bpb = beatsPerBar();
        for (const auto& placement : doc_->getPlacements()) {
            const auto* section = doc_->getSection(placement.section);
            if (section == nullptr)
                continue;
            const int count = total[placement.section.value];
            const auto letter =
                section->variations.size() > 1 ? synth::variationLetter(placement.variation) : juce::String();
            text.emplace_back(sectionstrip::blockTitle(section->name, placement.startBeat, section->lengthBeats, bpb,
                                                       ++seen[placement.section.value], count, letter),
                              sectionstrip::blockDescription(sectionstrip::blockTooltip(
                                  section->name, section->lengthBeats, bpb, count, platformCommandKeyName(), letter)));
        }
    }
    setDescription("Sections, " + juce::String(sectionCount) + (sectionCount == 1 ? " section" : " sections"));
    if (text == proxyText_ && proxies_.size() == text.size())
        return;

    proxyText_ = text;
    proxies_.clear();
    juce::Component::SafePointer<TimelineSectionStrip> safeThis(this);
    int index = 0;
    for (const auto& [title, description] : proxyText_) {
        auto proxy = std::make_unique<BlockProxy>();
        proxy->showVariations = [safeThis, i = index++] {
            auto* self = safeThis.getComponent();
            if (self != nullptr && self->doc_ != nullptr && i < (int)self->doc_->getPlacements().size())
                self->openVariationMenu(self->doc_->getPlacements()[(std::size_t)i].id);
        };
        // An overlay purely for accessibility: mouseDown() above still owns every real click.
        proxy->setInterceptsMouseClicks(false, false);
        proxy->setTitle(title);
        proxy->setDescription(description);
        addAndMakeVisible(*proxy);
        proxies_.push_back(std::move(proxy));
    }
    proxyLayoutKey_ = {};
    layoutProxies();
}

void TimelineSectionStrip::layoutProxies() {
    const juce::String key = juce::String(viewState_.firstVisibleBeat, 6) + "/" +
                             juce::String(viewState_.pixelsPerBeat, 6) + "/" + juce::String(getWidth()) + "x" +
                             juce::String(getHeight()) + "/" + juce::String((int)proxies_.size());
    if (key == proxyLayoutKey_ || doc_ == nullptr)
        return;
    proxyLayoutKey_ = key;
    std::size_t index = 0;
    for (const auto& placement : doc_->getPlacements()) {
        const auto* section = doc_->getSection(placement.section);
        if (section == nullptr || index >= proxies_.size())
            continue;
        const double x0 =
            std::clamp(xAtBeat(placement.startBeat), -(double)kMaxProxyCoordinate, (double)kMaxProxyCoordinate);
        const double x1 = std::clamp(xAtBeat(placement.startBeat + section->lengthBeats), -(double)kMaxProxyCoordinate,
                                     (double)kMaxProxyCoordinate);
        proxies_[index++]->setBounds(sectionstrip::blockRect((float)x0, (float)x1, getHeight()).toNearestInt());
    }
}

juce::String TimelineSectionStrip::getProxyTitleForTest(int index) const {
    return index >= 0 && index < (int)proxies_.size() ? proxies_[(size_t)index]->getTitle() : juce::String();
}

juce::String TimelineSectionStrip::getProxyDescriptionForTest(int index) const {
    return index >= 0 && index < (int)proxies_.size() ? proxies_[(size_t)index]->getDescription() : juce::String();
}

} // namespace synth::ui
