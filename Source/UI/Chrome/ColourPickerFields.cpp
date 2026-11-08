// ColourPickerFields.cpp -- the saturation/brightness square and the hue bar of the colour picker: drawing, the mouse
// (a press or drag sets the value under the pointer, as juce::ColourSelector's own views do), the keys (one percent or
// degree per arrow, ten with Shift, Home and End to the ends) and the screen-reader value.
#include "ColourPickerFields.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Layout/TooltipHelpHandler.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <cmath>

namespace synth::ui {

namespace {

constexpr float kHueDegrees = 360.0f;

class ValueText final : public juce::AccessibilityTextValueInterface {
public:
    explicit ValueText(std::function<juce::String()> text)
        : text_(std::move(text)) {}
    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return text_(); }
    void setValueAsString(const juce::String&) override {}

private:
    std::function<juce::String()> text_;
};

int stepOf(const juce::KeyPress& key) { return key.getModifiers().isShiftDown() ? 10 : 1; }

int toPercent(float value) { return (int)std::lround(value * 100.0f); }

float fromPercent(int percent) { return (float)juce::jlimit(0, 100, percent) / 100.0f; }

} // namespace

Hsb Hsb::fromColour(juce::Colour colour) {
    Hsb hsb;
    colour.getHSB(hsb.hue, hsb.saturation, hsb.brightness);
    return hsb;
}

ColourPickerField::ColourPickerField() { setWantsKeyboardFocus(true); }

void ColourPickerField::setHsb(const Hsb& hsb) {
    hsb_ = hsb;
    refreshDescription();
    repaint();
}

void ColourPickerField::userChanged(const Hsb& next) {
    hsb_ = next;
    refreshDescription();
    repaint();
    if (onChange)
        onChange(hsb_);
}

void ColourPickerField::refreshDescription() {
    setDescription(valueText());
    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
}

void ColourPickerField::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().reduced(kEdge);
    paintContent(g, area);
    const auto& colors = theme::themeOf(*this).colors;
    g.setColour(colors.border);
    g.drawRect(area.expanded(1), 1);
    paintMarker(g, area);
    paintFocusRing(g, getLocalBounds().toFloat(), *this, 2.0f);
}

std::unique_ptr<juce::AccessibilityHandler> ColourPickerField::createAccessibilityHandler() {
    return std::make_unique<TooltipHelpHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ValueText>([this] { return valueText(); })});
}

// ---- Saturation and brightness -------------------------------------------------------------------------------

SaturationBrightnessField::SaturationBrightnessField() {
    setComponentID("colourPickerSaturationBrightness");
    setTitle("Saturation and brightness");
    setTooltip("Left and Right change saturation, Up and Down change brightness; Shift for steps of 10");
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    refreshDescription();
}

juce::String SaturationBrightnessField::valueText() const {
    return "Saturation " + juce::String(toPercent(hsb_.saturation)) + "%, brightness " +
           juce::String(toPercent(hsb_.brightness)) + "%";
}

bool SaturationBrightnessField::keyPressed(const juce::KeyPress& key) {
    const int step = stepOf(key);
    Hsb next = hsb_;
    const int saturation = toPercent(hsb_.saturation);
    const int brightness = toPercent(hsb_.brightness);
    if (key.isKeyCode(juce::KeyPress::leftKey))
        next.saturation = fromPercent(saturation - step);
    else if (key.isKeyCode(juce::KeyPress::rightKey))
        next.saturation = fromPercent(saturation + step);
    else if (key.isKeyCode(juce::KeyPress::downKey))
        next.brightness = fromPercent(brightness - step);
    else if (key.isKeyCode(juce::KeyPress::upKey))
        next.brightness = fromPercent(brightness + step);
    else if (key.isKeyCode(juce::KeyPress::homeKey))
        next.saturation = 0.0f;
    else if (key.isKeyCode(juce::KeyPress::endKey))
        next.saturation = 1.0f;
    else
        return false;
    if (next.saturation != hsb_.saturation || next.brightness != hsb_.brightness)
        userChanged(next);
    return true;
}

void SaturationBrightnessField::mouseDrag(const juce::MouseEvent& e) {
    const auto area = getLocalBounds().reduced(kEdge);
    if (area.isEmpty())
        return;
    Hsb next = hsb_;
    next.saturation = juce::jlimit(0.0f, 1.0f, (float)(e.x - kEdge) / (float)area.getWidth());
    next.brightness = juce::jlimit(0.0f, 1.0f, 1.0f - (float)(e.y - kEdge) / (float)area.getHeight());
    userChanged(next);
}

void SaturationBrightnessField::paintContent(juce::Graphics& g, juce::Rectangle<int> area) {
    if (image_.isNull() || imageHue_ != hsb_.hue) {
        const int width = juce::jmax(1, area.getWidth() / 2);
        const int height = juce::jmax(1, area.getHeight() / 2);
        image_ = juce::Image(juce::Image::RGB, width, height, false);
        imageHue_ = hsb_.hue;
        juce::Image::BitmapData pixels(image_, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                pixels.setPixelColour(
                    x, y, juce::Colour(hsb_.hue, (float)x / (float)width, 1.0f - (float)y / (float)height, 1.0f));
    }
    g.setOpacity(1.0f);
    g.drawImage(image_, area.toFloat());
}

void SaturationBrightnessField::paintMarker(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto& colors = theme::themeOf(*this).colors;
    const auto centre = area.toFloat().getRelativePoint(hsb_.saturation, 1.0f - hsb_.brightness);
    g.setColour(colors.iconInk);
    g.drawEllipse(juce::Rectangle<float>(13.0f, 13.0f).withCentre(centre), 1.5f);
    g.setColour(colors.iconPaper);
    g.drawEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(centre), 1.5f);
}

// ---- Hue ---------------------------------------------------------------------------------------------------------

HueBar::HueBar() {
    setComponentID("colourPickerHue");
    setTitle("Hue");
    setTooltip("Left and Right, or Up and Down, change the hue; Shift for steps of 10 degrees");
    refreshDescription();
}

juce::String HueBar::valueText() const {
    return "Hue " + juce::String(juce::roundToInt(hsb_.hue * kHueDegrees)) + " degrees";
}

bool HueBar::keyPressed(const juce::KeyPress& key) {
    const int step = stepOf(key);
    const int degrees = juce::roundToInt(hsb_.hue * kHueDegrees);
    int next = degrees;
    if (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::upKey))
        next = degrees - step;
    else if (key.isKeyCode(juce::KeyPress::rightKey) || key.isKeyCode(juce::KeyPress::downKey))
        next = degrees + step;
    else if (key.isKeyCode(juce::KeyPress::homeKey))
        next = 0;
    else if (key.isKeyCode(juce::KeyPress::endKey))
        next = (int)kHueDegrees;
    else
        return false;
    next = juce::jlimit(0, (int)kHueDegrees, next);
    if (next != degrees) {
        Hsb value = hsb_;
        value.hue = (float)next / kHueDegrees;
        userChanged(value);
    }
    return true;
}

void HueBar::mouseDrag(const juce::MouseEvent& e) {
    const auto area = getLocalBounds().reduced(kEdge);
    if (area.isEmpty())
        return;
    Hsb next = hsb_;
    next.hue = juce::jlimit(0.0f, 1.0f, (float)(e.y - kEdge) / (float)area.getHeight());
    userChanged(next);
}

void HueBar::paintContent(juce::Graphics& g, juce::Rectangle<int> area) {
    juce::ColourGradient gradient;
    gradient.isRadial = false;
    gradient.point1 = {0.0f, (float)area.getY()};
    gradient.point2 = {0.0f, (float)area.getBottom()};
    for (float i = 0.0f; i <= 1.0f; i += 0.02f)
        gradient.addColour(i, juce::Colour(i, 1.0f, 1.0f, 1.0f));
    g.setGradientFill(gradient);
    g.fillRect(area);
}

void HueBar::paintMarker(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto& colors = theme::themeOf(*this).colors;
    const float y = (float)area.getY() + hsb_.hue * (float)area.getHeight();
    const auto thumb = juce::Rectangle<float>((float)area.getX() - 1.0f, y - 3.0f, (float)area.getWidth() + 2.0f, 6.0f);
    g.setColour(colors.iconInk);
    g.drawRoundedRectangle(thumb.expanded(1.0f), 2.0f, 1.5f);
    g.setColour(colors.iconPaper);
    g.drawRoundedRectangle(thumb, 2.0f, 1.5f);
}

} // namespace synth::ui
