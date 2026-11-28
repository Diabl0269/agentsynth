#include "CardPanelPicture.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>
#include <list>

namespace synth::ui {

namespace {

constexpr size_t kMaxPictures = 64;

struct Key {
    int width = 0;
    int height = 0;
    float scale = 1.0f;
    int headerHeight = 0;
    bool selected = false;
    bool bypassed = false;
    bool operator==(const Key&) const = default;
};

struct Entry {
    Key key;
    juce::Image image;
};

// Most recently used first. A theme change or another look-and-feel drops everything: the pictures hold its colours
// and treatment. Zoom steps add entries per scale; the cap keeps them bounded.
struct Store {
    std::list<Entry> entries;
    const synth::theme::AppLookAndFeel* lf = nullptr;
    std::uint64_t serial = 0;
    int renders = 0;
    bool enabled = true;
    bool software = false;
};

Store& store() {
    static Store s;
    return s;
}

juce::Image render(synth::theme::AppLookAndFeel& lf, const Key& key) {
    const int w = (int)std::ceil((float)key.width * key.scale);
    const int h = (int)std::ceil((float)key.height * key.scale);
    juce::Image image = store().software ? juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType())
                                         : juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics ig(image);
    ig.addTransform(juce::AffineTransform::scale(key.scale));
    lf.drawModulePanelBody(ig, {0.0f, 0.0f, (float)key.width, (float)key.height}, key.headerHeight, key.selected,
                           key.bypassed);
    ++store().renders;
    return image;
}

} // namespace

void CardPanelPicture::draw(juce::Graphics& g, synth::theme::AppLookAndFeel& lf, juce::Point<int> size,
                            int headerHeight, bool selected, bool bypassed) {
    auto& s = store();
    const juce::Rectangle<float> bounds(0.0f, 0.0f, (float)size.x, (float)size.y);
    if (!s.enabled) {
        lf.drawModulePanelBody(g, bounds, headerHeight, selected, bypassed);
        return;
    }
    const float scale = juce::jmax(0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (s.lf != &lf || s.serial != lf.getThemeSerial()) {
        s.entries.clear();
        s.lf = &lf;
        s.serial = lf.getThemeSerial();
    }
    const Key key{size.x, size.y, scale, headerHeight, selected, bypassed};
    auto it = std::find_if(s.entries.begin(), s.entries.end(), [&](const Entry& e) { return e.key == key; });
    if (it == s.entries.end()) {
        s.entries.push_front({key, render(lf, key)});
        if (s.entries.size() > kMaxPictures)
            s.entries.pop_back();
        it = s.entries.begin();
    } else {
        s.entries.splice(s.entries.begin(), s.entries, it);
    }
    g.saveState();
    g.addTransform(juce::AffineTransform::scale(1.0f / scale)); // the picture's pixels land 1:1 on the device's
    g.drawImageAt(it->image, 0, 0);
    g.restoreState();
}

int CardPanelPicture::renderCountForTest() noexcept { return store().renders; }

void CardPanelPicture::resetForTest() {
    auto& s = store();
    s.entries.clear();
    s.lf = nullptr;
    s.renders = 0;
}

void CardPanelPicture::setEnabledForTest(bool enabled) noexcept { store().enabled = enabled; }
void CardPanelPicture::setSoftwareImagesForTest(bool software) noexcept { store().software = software; }

} // namespace synth::ui
