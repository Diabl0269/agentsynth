#include "UI/Assistant/AIChatComponent/ChatHistoryPicker.h"
#include "UI/Graph/ModMatrixPicker.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::chat_history {

namespace {

constexpr int kFooterHeight = 30;
constexpr int kClearButtonWidth = 120;

using synth::ui::ModMatrixPicker;

// The strip under the list. A parentless call-out does not inherit the app's look, so its colours come from the theme.
class ClearStrip : public juce::Component {
public:
    explicit ClearStrip(std::function<void()> onPressed) {
        setComponentID("aiHistoryClearStrip");
        clear_.setButtonText("Clear my history");
        clear_.setTitle("Clear my history");
        clear_.setTooltip("Delete every saved conversation. You are asked to confirm first.");
        clear_.setComponentID("aiHistoryClear");
        clear_.onClick = std::move(onPressed);
        addAndMakeVisible(clear_);
    }

    juce::TextButton& button() { return clear_; }

    void resized() override { clear_.setBounds(getLocalBounds().removeFromRight(kClearButtonWidth)); }

    void paint(juce::Graphics& g) override {
        g.setColour(synth::theme::themeOf(*this).colors.border);
        g.drawHorizontalLine(0, 0.0f, (float)getWidth());
    }

private:
    juce::TextButton clear_;
};

ModMatrixPicker* asPicker(juce::Component& c) { return dynamic_cast<ModMatrixPicker*>(&c); }

} // namespace

std::unique_ptr<juce::Component> makePicker(const std::vector<Entry>& entries, Hooks hooks) {
    std::vector<ModMatrixPicker::Item> items;
    items.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        ModMatrixPicker::Item item;
        item.id = (int)i + 1;
        item.text = entries[i].title.isNotEmpty() ? entries[i].title : juce::String("Untitled");
        item.detail = entries[i].date;
        items.push_back(std::move(item));
    }
    if (items.empty()) {
        ModMatrixPicker::Item none;
        none.id = -1;
        none.text = "No saved conversations";
        none.enabled = false;
        items.push_back(std::move(none));
    }

    // The ids are copied: the picker outlives the caller's list.
    auto ids = std::make_shared<std::vector<juce::String>>();
    for (const auto& e : entries)
        ids->push_back(e.id);
    auto onOpen = hooks.onOpen;
    auto picker = std::make_unique<ModMatrixPicker>("chat", std::move(items), 0, [ids, onOpen](int id) {
        if (id >= 1 && (size_t)id <= ids->size() && onOpen)
            onOpen((*ids)[(size_t)id - 1]);
    });
    picker->setComponentID("aiHistoryList");
    picker->setAccessibleNames("Saved conversations", "Search saved conversations");
    picker->setSearchPlaceholder("Search conversations...");

    auto* raw = picker.get();
    auto onClear = hooks.onClear;
    auto strip = std::make_unique<ClearStrip>([raw, onClear] {
        // dismiss() closes the call-out, which deletes this very button: run the callback on the next turn.
        raw->dismiss();
        juce::MessageManager::callAsync([onClear] {
            if (onClear)
                onClear();
        });
    });
    picker->setFooter(std::move(strip), kFooterHeight);
    return picker;
}

int listedCountForTest(juce::Component& picker) {
    auto* p = asPicker(picker);
    if (p == nullptr)
        return 0;
    return (int)p->getVisibleItemTextsForTest().size();
}

int builtRowCountForTest(juce::Component& picker) {
    auto* p = asPicker(picker);
    return p != nullptr ? p->getRowComponentCountForTest() : 0;
}

std::vector<juce::String> visibleTitlesForTest(juce::Component& picker) {
    auto* p = asPicker(picker);
    return p != nullptr ? p->getVisibleItemTextsForTest() : std::vector<juce::String>{};
}

std::vector<juce::String> visibleDatesForTest(juce::Component& picker) {
    auto* p = asPicker(picker);
    return p != nullptr ? p->getVisibleItemDetailsForTest() : std::vector<juce::String>{};
}

bool sendKeyForTest(juce::Component& picker, const juce::KeyPress& key) {
    auto* p = asPicker(picker);
    return p != nullptr && p->sendKeyForTest(key);
}

void chooseRowForTest(juce::Component& picker, int visibleIndex) {
    if (auto* p = asPicker(picker))
        p->chooseVisibleItemForTest(visibleIndex);
}

void pressClearForTest(juce::Component& picker) {
    auto* p = asPicker(picker);
    if (p == nullptr || p->getFooter() == nullptr)
        return;
    if (auto* strip = dynamic_cast<ClearStrip*>(p->getFooter()))
        strip->button().triggerClick();
}

int heightForTest(juce::Component& picker) { return picker.getHeight(); }

} // namespace synth::chat_history
