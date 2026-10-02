#pragma once

#include <array>
#include <cstddef>

namespace synth::ui {

// What the Draw tool puts on an automation lane. Free is the freehand pen (Shift+drag still draws a
// straight line), Line is a straight line, and the periodic shapes are stamped into a dragged box or
// over a lane range. The enumerator order is the shape strip's left-to-right order and the Shift+digit
// default of each shape's shortcut (Free = Shift+1). Append new shapes at the end.
enum class DrawShape {
    Free,
    Line,
    Sine,
    Triangle,
    Saw,
    Square,
};

inline constexpr std::array<DrawShape, 6> kAllDrawShapes{
    DrawShape::Free, DrawShape::Line, DrawShape::Sine, DrawShape::Triangle, DrawShape::Saw, DrawShape::Square,
};

/** True for the shapes that repeat once per cycle (stamped into a box), false for Free and Line. */
constexpr bool isPeriodicShape(DrawShape shape) noexcept {
    return shape == DrawShape::Sine || shape == DrawShape::Triangle || shape == DrawShape::Saw ||
           shape == DrawShape::Square;
}

/** The shape's one-word name: "Sine". The button title is this plus " shape". */
constexpr const char* drawShapeName(DrawShape shape) noexcept {
    switch (shape) {
    case DrawShape::Free:
        return "Free";
    case DrawShape::Line:
        return "Line";
    case DrawShape::Sine:
        return "Sine";
    case DrawShape::Triangle:
        return "Triangle";
    case DrawShape::Saw:
        return "Saw";
    case DrawShape::Square:
        return "Square";
    }
    return "Free";
}

/** The digit of the shape's default Shift+digit shortcut (1..6). */
constexpr int drawShapeKeyDigit(DrawShape shape) noexcept { return static_cast<int>(shape) + 1; }

/** The next shape in strip order, wrapping from the last back to Free. */
constexpr DrawShape nextDrawShape(DrawShape shape) noexcept {
    const auto next = static_cast<std::size_t>(shape) + 1;
    return next < kAllDrawShapes.size() ? kAllDrawShapes[next] : kAllDrawShapes[0];
}

} // namespace synth::ui
