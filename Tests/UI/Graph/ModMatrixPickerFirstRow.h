#pragma once

// A picker opens at the top of its list: the viewport's view top is 0 and the first header (or first row) is in view.
// Header-only; not compiled on its own. ModMatrixPickerFirstRowTests.cpp lists every producer.

#include "UI/Graph/ModMatrixPicker.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace first_row_test {

inline void expectAtFirstRow(synth::ui::ModMatrixPicker& picker, const char* when) {
    EXPECT_EQ(picker.getViewTopForTest(), 0) << when;
    EXPECT_TRUE(picker.isFirstEntryInViewForTest()) << when << ": the first line is not in view";
}

/** Checks `picker` straight after construction, after it is sized the way setFooter does, and after it sits in a
 *  CallOutBox (parented to a plain component, so no desktop is needed). Name the producer for the failure message. */
inline void expectOpensAtFirstRow(synth::ui::ModMatrixPicker& picker, const juce::String& producer) {
    SCOPED_TRACE(producer.toStdString());
    ASSERT_FALSE(picker.getVisibleRowNamesForTest().empty());
    expectAtFirstRow(picker, "after construction");

    picker.setFooter(std::make_unique<juce::Component>(), 28);
    expectAtFirstRow(picker, "after setFooter");
    picker.clearFooter();
    expectAtFirstRow(picker, "after clearFooter");

    juce::Component host;
    host.setSize(1200, 900);
    juce::CallOutBox box(picker, juce::Rectangle<int>(400, 300, 120, 24), &host);
    box.setVisible(true);
    expectAtFirstRow(picker, "inside its call-out");
}

} // namespace first_row_test
