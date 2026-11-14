// FineDragSlider.cpp -- the factory for headers that cannot include FineDragSlider.h.
#include "UI/Controls/FineDrag/FineDragSlider.h"

namespace synth::ui {

std::unique_ptr<juce::Slider> makeFineDragSlider() { return std::make_unique<FineDragSlider>(); }

} // namespace synth::ui
