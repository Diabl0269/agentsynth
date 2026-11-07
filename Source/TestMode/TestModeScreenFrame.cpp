// Concern: the composited screen frame of the test-mode `screenshot` (see TestModeScreenFrame.h).

#include "TestMode/TestModeScreenFrame.h"
#include "TestMode/TestModeComponents.h"
#include "TestMode/TestModeInput.h"
#include "TestMode/TestModePresentedFrame.h"
#include "UI/Timeline/ToolCursors.h"

namespace synth::testmode {

namespace {

struct Capture {
    juce::Component* component = nullptr;
    juce::Rectangle<int> screenBounds;
};

juce::String windowId(const juce::Component& c) {
    if (c.getComponentID().isNotEmpty())
        return c.getComponentID();
    return c.getName().isNotEmpty() ? c.getName() : typeNameOf(c);
}

// The main window first, then every other visible top-level component that has a size, in the order the desktop
// stacks them from the back, so the later ones draw over the earlier.
std::vector<Capture> windowsToCapture(juce::Component& main) {
    std::vector<Capture> out{{&main, main.getScreenBounds()}};
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent(i);
            top != nullptr && top != &main && top->isVisible() && !top->getLocalBounds().isEmpty())
            out.push_back({top, top->getScreenBounds()});
    return out;
}

// The main window's, plus the others cut to the display the main window is on (a window parked far away is not
// something a user sees).
juce::Rectangle<int> canvasBounds(const std::vector<Capture>& captures) {
    auto canvas = captures.front().screenBounds;
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(canvas);
    for (std::size_t i = 1; i < captures.size(); ++i) {
        auto bounds = captures[i].screenBounds;
        if (display != nullptr)
            bounds = bounds.getIntersection(display->totalArea);
        canvas = canvas.getUnion(bounds);
    }
    return canvas;
}

void drawArrow(juce::Graphics& g, juce::Point<float> tip, float scale) {
    juce::Path arrow;
    arrow.startNewSubPath(0.0f, 0.0f);
    for (const auto& p :
         {juce::Point<float>(0.0f, 17.0f), {4.0f, 13.0f}, {7.0f, 20.0f}, {10.0f, 19.0f}, {7.0f, 12.0f}, {12.0f, 12.0f}})
        arrow.lineTo(p);
    arrow.closeSubPath();
    arrow.applyTransform(juce::AffineTransform::scale(scale).translated(tip.x, tip.y));
    g.setColour(juce::Colours::white);
    g.fillPath(arrow);
    g.setColour(juce::Colour(0xff101010));
    g.strokePath(arrow, juce::PathStrokeType(1.2f * scale, juce::PathStrokeType::mitered));
}

// Draws the pointer at `tip` (frame pixels): the cursor image of the component under it when that has one, else an
// arrow. Returns the kind.
juce::String drawPointer(juce::Graphics& g, juce::Point<int> screenPoint, juce::Point<float> tip, float scale) {
    if (auto* under = juce::Desktop::getInstance().findComponentAt(screenPoint))
        if (const auto* custom = synth::ui::findToolCursorImage(under->getMouseCursor())) {
            const auto size = custom->image.getBounds().toFloat() * scale;
            g.drawImage(custom->image, juce::Rectangle<float>(tip.x - (float)custom->hotspot.x * scale,
                                                              tip.y - (float)custom->hotspot.y * scale, size.getWidth(),
                                                              size.getHeight()));
            return custom->name;
        }
    drawArrow(g, tip, scale);
    return "arrow";
}

} // namespace

std::optional<ScreenFrame> captureScreenFrame(float scale, juce::String& error) {
    auto* main = findMainWindow();
    if (main == nullptr || main->getLocalBounds().isEmpty()) {
        error = "no window to capture";
        return std::nullopt;
    }
    const ScopedOffscreenPaint offscreen; // not what the screen shows: the presented frame ignores it
    const auto captures = windowsToCapture(*main);
    const auto canvas = canvasBounds(captures);
    const auto pixels = canvas.toFloat() * scale;
    ScreenFrame frame;
    frame.image = juce::Image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt(pixels.getWidth())),
                              juce::jmax(1, juce::roundToInt(pixels.getHeight())), true);
    juce::Graphics g(frame.image);
    g.fillAll(juce::Colours::black);
    for (const auto& capture : captures) {
        const auto shot = capture.component->createComponentSnapshot(capture.component->getLocalBounds(), true, scale);
        if (!shot.isValid())
            continue;
        const auto at = capture.screenBounds.getPosition() - canvas.getPosition();
        g.drawImageAt(shot, juce::roundToInt((float)at.x * scale), juce::roundToInt((float)at.y * scale));
        frame.windows.push_back({windowId(*capture.component), capture.screenBounds.withPosition(at)});
    }
    // Before the first synthetic move the pointer rests at the main window's corner of the frame.
    const auto screenPoint = lastPointerScreenPoint().value_or(captures.front().screenBounds.getPosition());
    frame.pointer = screenPoint - canvas.getPosition();
    frame.pointerKind = drawPointer(g, screenPoint, (frame.pointer.toFloat() * scale), scale);
    return frame;
}

juce::var describeScreenFrame(const ScreenFrame& frame, const juce::String& path) {
    juce::Array<juce::var> windows;
    for (const auto& w : frame.windows) {
        auto* o = new juce::DynamicObject();
        o->setProperty("id", w.id);
        o->setProperty("x", w.bounds.getX());
        o->setProperty("y", w.bounds.getY());
        o->setProperty("width", w.bounds.getWidth());
        o->setProperty("height", w.bounds.getHeight());
        windows.add(juce::var(o));
    }
    auto* pointer = new juce::DynamicObject();
    pointer->setProperty("x", frame.pointer.x);
    pointer->setProperty("y", frame.pointer.y);
    pointer->setProperty("kind", frame.pointerKind);
    auto* o = new juce::DynamicObject();
    o->setProperty("path", path);
    o->setProperty("width", frame.image.getWidth());
    o->setProperty("height", frame.image.getHeight());
    o->setProperty("windows", windows);
    o->setProperty("pointer", juce::var(pointer));
    return juce::var(o);
}

} // namespace synth::testmode
