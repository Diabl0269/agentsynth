#pragma once

#include "ShortcutManager/AppCommands.h"
#include <array>

/** The Set Grid commands of the triplet and dotted snap values, in grid order: the eight triplets (1/1T to 1/128T),
 * then the eight dotted values (1/1D to 1/128D). Entry i is kind i / 8 and bottom number 1 << (i % 8). */
struct SnapNoteValueAction {
    const char* actionId;
    const char* label; // "1/8T"
    juce::CommandID command;
};

inline const std::array<SnapNoteValueAction, 16>& snapNoteValueActions() {
    static const std::array<SnapNoteValueAction, 16> actions{{
        {"snapSetWholeTriplet", "1/1T", AppCommands::snapSetWholeTriplet},
        {"snapSetHalfTriplet", "1/2T", AppCommands::snapSetHalfTriplet},
        {"snapSetQuarterTriplet", "1/4T", AppCommands::snapSetQuarterTriplet},
        {"snapSetEighthTriplet", "1/8T", AppCommands::snapSetEighthTriplet},
        {"snapSetSixteenthTriplet", "1/16T", AppCommands::snapSetSixteenthTriplet},
        {"snapSetThirtySecondTriplet", "1/32T", AppCommands::snapSetThirtySecondTriplet},
        {"snapSetSixtyFourthTriplet", "1/64T", AppCommands::snapSetSixtyFourthTriplet},
        {"snapSetHundredTwentyEighthTriplet", "1/128T", AppCommands::snapSetHundredTwentyEighthTriplet},
        {"snapSetWholeDotted", "1/1D", AppCommands::snapSetWholeDotted},
        {"snapSetHalfDotted", "1/2D", AppCommands::snapSetHalfDotted},
        {"snapSetQuarterDotted", "1/4D", AppCommands::snapSetQuarterDotted},
        {"snapSetEighthDotted", "1/8D", AppCommands::snapSetEighthDotted},
        {"snapSetSixteenthDotted", "1/16D", AppCommands::snapSetSixteenthDotted},
        {"snapSetThirtySecondDotted", "1/32D", AppCommands::snapSetThirtySecondDotted},
        {"snapSetSixtyFourthDotted", "1/64D", AppCommands::snapSetSixtyFourthDotted},
        {"snapSetHundredTwentyEighthDotted", "1/128D", AppCommands::snapSetHundredTwentyEighthDotted},
    }};
    return actions;
}
