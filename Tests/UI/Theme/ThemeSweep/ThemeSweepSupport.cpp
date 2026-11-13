// ThemeSweepSupport.cpp -- see ThemeSweepSupport.h.
#include "ThemeSweepSupport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace synth::test::theme_sweep {

namespace {

double relativeLuminance(juce::Colour c) {
    auto channel = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
}

juce::Colour opaqueOver(juce::Colour top, juce::Colour base) { return base.withAlpha(1.0f).overlaidWith(top); }

// The channel-wise median of a few colours: robust against the odd glyph pixel in a sample row.
juce::Colour medianColour(std::vector<juce::Colour> colours) {
    const auto mid = colours.size() / 2;
    auto pick = [&](auto channel) {
        std::vector<int> values;
        for (const auto& c : colours)
            values.push_back((int)channel(c));
        std::nth_element(values.begin(), values.begin() + (long)mid, values.end());
        return (juce::uint8)values[mid];
    };
    return juce::Colour(
        pick([](juce::Colour c) { return c.getRed(); }), pick([](juce::Colour c) { return c.getGreen(); }),
        pick([](juce::Colour c) { return c.getBlue(); }), pick([](juce::Colour c) { return c.getAlpha(); }));
}

// Along a row 2 px inside the top edge of `bounds`, away from the middle where the text sits.
juce::Colour sampleBackground(const juce::Image& image, juce::Rectangle<int> bounds, juce::Colour underlay,
                              bool skipLeft) {
    std::vector<juce::Colour> samples;
    const int y = juce::jlimit(0, image.getHeight() - 1, bounds.getY() + 2);
    // A toggle's switch sits at its left end, so its row is read from the right.
    for (const float fraction : skipLeft ? std::array<float, 5>{0.6f, 0.7f, 0.8f, 0.9f, 0.95f}
                                         : std::array<float, 5>{0.12f, 0.3f, 0.5f, 0.7f, 0.88f}) {
        const int x = juce::jlimit(0, image.getWidth() - 1, bounds.getX() + (int)((float)bounds.getWidth() * fraction));
        samples.push_back(image.getPixelAt(x, y));
    }
    return opaqueOver(medianColour(samples), underlay);
}

bool hasText(const juce::String& text) { return text.trim().isNotEmpty(); }

// The colour a control draws its text with, or nothing when `c` is not a text control.
bool textOf(juce::Component& c, juce::String& kind, juce::String& text, juce::Colour& colour) {
    if (auto* label = dynamic_cast<juce::Label*>(&c)) {
        kind = "Label";
        text = label->getText();
        colour = label->findColour(juce::Label::textColourId);
    } else if (auto* toggle = dynamic_cast<juce::ToggleButton*>(&c)) {
        // A pill toggle that is on draws its text in the page colour on the accent, not in textColourId: not judged.
        if (toggle->getToggleState())
            return false;
        kind = "Toggle";
        text = toggle->getButtonText();
        colour = toggle->findColour(juce::ToggleButton::textColourId);
    } else if (auto* button = dynamic_cast<juce::TextButton*>(&c)) {
        kind = "Button";
        text = button->getButtonText();
        colour = button->findColour(button->getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId);
    } else if (auto* combo = dynamic_cast<juce::ComboBox*>(&c)) {
        kind = "Combo";
        text = combo->getText();
        colour = combo->findColour(juce::ComboBox::textColourId);
    } else {
        return false;
    }
    return true;
}

void collectInto(juce::Component& root, juce::Component& c, const juce::Image& image, juce::Colour underlay,
                 std::vector<TextSample>& out) {
    // The root is rendered by the caller whether or not it is shown (an alert box is built but never launched).
    if ((&c != &root && !c.isVisible()) || !c.isEnabled())
        return; // a disabled control is exempt from the contrast rule (WCAG 1.4.3)
    juce::String kind, text;
    juce::Colour colour;
    // A fully transparent text colour means the control does not draw its text with it (an alert box's message).
    if (textOf(c, kind, text, colour) && hasText(text) && !colour.isTransparent()) {
        const auto bounds = root.getLocalArea(&c, c.getLocalBounds()).getIntersection(root.getLocalBounds());
        if (!bounds.isEmpty()) {
            TextSample sample;
            sample.description = kind + " '" + text.replaceCharacters("\r\n", "  ").substring(0, 40) + "'";
            sample.text = colour;
            sample.background = sampleBackground(image, bounds, underlay, kind == "Toggle");
            sample.ratio = contrastRatio(colour, sample.background);
            out.push_back(sample);
        }
    }
    for (auto* child : c.getChildren())
        collectInto(root, *child, image, underlay, out);
}

} // namespace

juce::Image renderToImage(juce::Component& root) {
    // SoftwareImageType: the platform default image reads back zeros on some runners.
    juce::Image image(juce::Image::ARGB, juce::jmax(1, root.getWidth()), juce::jmax(1, root.getHeight()), true,
                      juce::SoftwareImageType());
    juce::Graphics g(image);
    root.paintEntireComponent(g, false);
    return image;
}

double contrastRatio(juce::Colour text, juce::Colour background) {
    const auto bg = background.withAlpha(1.0f);
    const auto l1 = relativeLuminance(opaqueOver(text, bg));
    const auto l2 = relativeLuminance(bg);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

void writeSweepPng(const juce::String& surfaceName, const juce::String& themeId, const juce::Image& image) {
    if (std::getenv("AGENTSYNTH_WRITE_THEME_PNG") == nullptr)
        return;
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("build-ci-local/theme-sweep");
    dir.createDirectory();
    const auto file = dir.getChildFile(slug(surfaceName) + "-" + themeId + ".png");
    if (auto out = file.createOutputStream()) {
        out->setPosition(0);
        out->truncate();
        juce::PNGImageFormat().writeImageToStream(image, *out);
    }
}

std::vector<TextSample> collectTextSamples(juce::Component& root, const juce::Image& image, juce::Colour underlay) {
    std::vector<TextSample> samples;
    collectInto(root, root, image, underlay, samples);
    return samples;
}

juce::String slug(const juce::String& name) {
    juce::String out;
    for (auto c : name.toLowerCase())
        out << (juce::CharacterFunctions::isLetterOrDigit(c) ? juce::String::charToString(c) : juce::String("-"));
    return out;
}

} // namespace synth::test::theme_sweep
