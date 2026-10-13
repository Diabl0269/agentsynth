// Concern: the pictures on the lane header's control chips, drawn as paths on a 14 px grid (so they look the same in
// the headless test build and at any chip size).
#include "UI/Timeline/AutomationLanes/LaneHeaderParts/LaneControls.h"

#include <algorithm>

namespace synth::ui {

namespace {
// A 14 x 14 drawing grid centred in the shorter side of `area`.
struct Grid {
    explicit Grid(juce::Rectangle<float> area) {
        const float side = std::min(area.getWidth(), area.getHeight());
        origin = juce::Rectangle<float>(side, side).withCentre(area.getCentre()).getPosition();
        scale = side / 14.0f;
    }
    juce::Point<float> at(float x, float y) const { return {origin.x + x * scale, origin.y + y * scale}; }
    juce::PathStrokeType stroke(float width = 1.4f) const {
        return {width * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded};
    }
    juce::Rectangle<float> rect(float x, float y, float w, float h) const {
        return {origin.x + x * scale, origin.y + y * scale, w * scale, h * scale};
    }
    juce::Point<float> origin;
    float scale = 1.0f;
};

void stroke(juce::Graphics& g, const Grid& grid, const juce::Path& path, float width = 1.4f) {
    g.strokePath(path, grid.stroke(width));
}

juce::Path line(const Grid& grid, float x1, float y1, float x2, float y2) {
    juce::Path path;
    path.startNewSubPath(grid.at(x1, y1));
    path.lineTo(grid.at(x2, y2));
    return path;
}

juce::Path wave(const Grid& grid, float x, float y, float w, float h) {
    juce::Path path;
    path.startNewSubPath(grid.at(x, y + h / 2));
    path.cubicTo(grid.at(x + w * 0.18f, y), grid.at(x + w * 0.32f, y), grid.at(x + w / 2, y + h / 2));
    path.cubicTo(grid.at(x + w * 0.68f, y + h), grid.at(x + w * 0.82f, y + h), grid.at(x + w, y + h / 2));
    return path;
}

void paintReadWrite(juce::Graphics& g, const Grid& grid) {
    juce::Path path;
    path.startNewSubPath(grid.at(1, 10));
    path.lineTo(grid.at(4.5f, 5));
    path.lineTo(grid.at(8, 8));
    path.lineTo(grid.at(13, 3));
    stroke(g, grid, path);
    stroke(g, grid, line(grid, 8, 1, 8, 13), 1.0f);
    g.fillEllipse(juce::Rectangle<float>(3.0f * grid.scale, 3.0f * grid.scale).withCentre(grid.at(8, 8)));
}

void paintAddMod(juce::Graphics& g, const Grid& grid, juce::Colour colour) {
    stroke(g, grid, wave(grid, 1, 6, 9, 6));
    g.setColour(juce::Colour(0xff3ecf8e));
    stroke(g, grid, line(grid, 11, 1.5f, 11, 6.5f));
    stroke(g, grid, line(grid, 8.5f, 4, 13.5f, 4));
    g.setColour(colour);
}

void paintDial(juce::Graphics& g, const Grid& grid) {
    g.drawEllipse(grid.rect(2, 3, 10, 10).reduced(0.7f * grid.scale), 1.4f * grid.scale);
    stroke(g, grid, line(grid, 7, 8, 10, 4.5f));
}

void paintMute(juce::Graphics& g, const Grid& grid) {
    juce::Path speaker;
    speaker.startNewSubPath(grid.at(1.5f, 5.5f));
    speaker.lineTo(grid.at(4.5f, 5.5f));
    speaker.lineTo(grid.at(7.5f, 3));
    speaker.lineTo(grid.at(7.5f, 11));
    speaker.lineTo(grid.at(4.5f, 8.5f));
    speaker.lineTo(grid.at(1.5f, 8.5f));
    speaker.closeSubPath();
    g.fillPath(speaker);
    stroke(g, grid, line(grid, 9.5f, 5.2f, 12.5f, 8.8f));
    stroke(g, grid, line(grid, 12.5f, 5.2f, 9.5f, 8.8f));
}

void paintDuplicate(juce::Graphics& g, const Grid& grid) {
    g.drawRoundedRectangle(grid.rect(1.5f, 4, 7, 8.5f), 1.2f * grid.scale, 1.3f * grid.scale);
    g.drawRoundedRectangle(grid.rect(5.5f, 1.5f, 7, 8.5f), 1.2f * grid.scale, 1.3f * grid.scale);
}

void paintMove(juce::Graphics& g, const Grid& grid) {
    juce::Path arrow;
    arrow.startNewSubPath(grid.at(7, 1.5f));
    arrow.lineTo(grid.at(7, 9));
    arrow.startNewSubPath(grid.at(3.8f, 6));
    arrow.lineTo(grid.at(7, 9.2f));
    arrow.lineTo(grid.at(10.2f, 6));
    stroke(g, grid, arrow);
    stroke(g, grid, line(grid, 2.5f, 12, 11.5f, 12));
}

void paintCanvas(juce::Graphics& g, const Grid& grid) {
    g.drawRoundedRectangle(grid.rect(1.5f, 2.5f, 11, 9), 1.5f * grid.scale, 1.3f * grid.scale);
    g.fillRoundedRectangle(grid.rect(4, 5, 4.5f, 4), 0.8f * grid.scale);
}

void paintSwap(juce::Graphics& g, const Grid& grid) {
    juce::Path arrows;
    arrows.startNewSubPath(grid.at(2, 4.5f));
    arrows.lineTo(grid.at(12, 4.5f));
    arrows.startNewSubPath(grid.at(9.5f, 2));
    arrows.lineTo(grid.at(12, 4.5f));
    arrows.lineTo(grid.at(9.5f, 7));
    arrows.startNewSubPath(grid.at(12, 9.5f));
    arrows.lineTo(grid.at(2, 9.5f));
    arrows.startNewSubPath(grid.at(4.5f, 7));
    arrows.lineTo(grid.at(2, 9.5f));
    arrows.lineTo(grid.at(4.5f, 12));
    stroke(g, grid, arrows);
}

void paintCustomLfo(juce::Graphics& g, const Grid& grid) {
    stroke(g, grid, wave(grid, 3, 4, 8, 6));
    stroke(g, grid, line(grid, 1.5f, 2, 1.5f, 12), 1.1f);
    stroke(g, grid, line(grid, 12.5f, 2, 12.5f, 12), 1.1f);
}

void paintRemove(juce::Graphics& g, const Grid& grid) {
    stroke(g, grid, line(grid, 3.5f, 3.5f, 10.5f, 10.5f), 1.6f);
    stroke(g, grid, line(grid, 10.5f, 3.5f, 3.5f, 10.5f), 1.6f);
}
} // namespace

void paintLaneControlIcon(juce::Graphics& g, LaneControl control, juce::Rectangle<float> area, juce::Colour colour) {
    const Grid grid(area);
    g.setColour(colour);
    switch (control) {
    case LaneControl::ReadWrite:
        paintReadWrite(g, grid);
        break;
    case LaneControl::AddMod:
        paintAddMod(g, grid, colour);
        break;
    case LaneControl::ChangeParameter:
        paintDial(g, grid);
        break;
    case LaneControl::ChangeSource:
        paintSwap(g, grid);
        break;
    case LaneControl::Mute:
        paintMute(g, grid);
        break;
    case LaneControl::Duplicate:
        paintDuplicate(g, grid);
        break;
    case LaneControl::MoveToTrack:
        paintMove(g, grid);
        break;
    case LaneControl::ShowInCanvas:
        paintCanvas(g, grid);
        break;
    case LaneControl::CreateCustomLfo:
        paintCustomLfo(g, grid);
        break;
    case LaneControl::Remove:
        paintRemove(g, grid);
        break;
    }
}

void paintLanePlusIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
    const Grid grid(area);
    g.setColour(colour);
    stroke(g, grid, line(grid, 7, 2.5f, 7, 11.5f), 1.6f);
    stroke(g, grid, line(grid, 2.5f, 7, 11.5f, 7), 1.6f);
}

void paintLanePencilIcon(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour) {
    const Grid grid(area);
    g.setColour(colour);
    stroke(g, grid, line(grid, 3, 11, 10.5f, 3.5f), 2.4f);
    stroke(g, grid, line(grid, 2.5f, 12, 3.2f, 10.3f), 1.4f);
    stroke(g, grid, line(grid, 8.5f, 12, 12, 12), 1.2f);
}

} // namespace synth::ui
