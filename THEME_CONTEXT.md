Issue Scope (Issue #102)
This issue trackstheme remapping for components that still use hardcoded colors:
- FrequencyResponseComponent — full paint method uses hardcoded JUCE colours
- ScopeComponent — partially themed (has layout fallback but not fully migrated)  
- AIChatComponent — chat bubbles/console use hardcoded colours
- MidiKeyboardComponent — requires linking juce_audio_utils for ColourIds
None of these components currently query the theme system.
Hardcoded Colours in Each Component
FrequencyResponseComponent.h (lines 143–296)
Use	Hardcoded colour	Hex
Background fill	juce::Colour(0xff1a1a2e)	#1a1a2e
Vertical grid (freq)	juce::Colour(0xff2a2a3e)	#2a2a3e
Hz labels (muted text)	juce::Colour(0xff6a6a7e)	#6a6a7e
db labels (left edge)	juce::Colour(0xff6a6a7e)	same as above
Gradient fill top	juce::Colour(0x6000b4d8)	#00b4d8
Gradient fill bottom	juce::Colour(0x1000b4d8)	#00b4d8
Curve stroke	juce::Colour(0xff00b4d8)	#00b4d8
Peak dot outline	juce::Colour(0xff1a1a2e)	#1a1a2e (bg)
Peak dot fill	juce::Colour(0xff00b4d8)	same as curve
Callout text	juce::Colour(0xcc00b4d8)	#00b4d8
Spectrum fill top	juce::Colour(0x3066cc66)	#66cc66
Spectrum fill bottom	juce::Colour(0x0866cc66)	#66cc66
Spectrum stroke	juce::Colour(0xaa66cc66)	#66cc66
Cutoff marker	juce::Colour(0x4000b4d8)	#00b4d8
ScopeComponent.h (lines 43–109)
Use	Hardcoded colour	Hex
Background fill	juce::Colours::black	#000000
Fallback grid	juce::Colour(0xff2A2F38)	#2A2F38 (same as border)
Fallback muted text	juce::Colour(0xff5C6470)	#5C6470 (same as textDisabled)
Fallback wave	juce::Colours::limegreen	#32CD32
ScopeComponent already has themed fallback logic: if a cast succeeds, it uses:
- colors.border.withAlpha(0.6f)
- colors.textDisabled
- colors.accent
AIChatComponent.h & .cpp
Header (SpinnerDot line 74):
Use	Hardcoded colour
Spinner fill	juce::Colours::lightblue
Source (AIChatComponent.cpp):
Line	Use	Hardcoded colour
36, 37	Header label (non-merge/merge)	juce::Colours::lightyellow, juce::Colours::lightgreen
51	Apply button (non-merge/merge)	juce::Colours::darkgreen, juce::Colour(0xFF8B6914)
59	JSON editor background	juce::Colours::black.withAlpha(0.3f)
117, 118	User/AI bubble base & gradient bottom	juce::Colours::blue, darkgrey.brighter(0.2f)
124	Bubble outline	juce::Colours::white.withAlpha(0.15f)
128	Role text (user/AI)	juce::Colours::lightblue, juce::Colours::grey
205	Placeholder text	juce::Colours::grey
215	Cancel button	juce::Colours::darkred
369	paint background fill	juce::Colours::darkgrey.darker(0.5f)
Theme Tokens Exposed by GravisynthLookAndFeel
Theme.h (lines 17–41) defines the Colors struct with these semantic tokens:
juce:: Colour bg0{0xff0B0D10};             // deepest page / window background
juce::Colour bg1{0xff13161B};              // canvas / graph editor background
juce::Colour surface{0xff1B1F26};          // module cards / panels
juce::Colour surfaceHi{0xff232833};        // raised surface / card top gradient stop
juce::Colour border{0xff2A2F38};           // hairline borders
juce::Colour accent{0xff00D1FF};           // primary accent (selection, value arc)
juce::Colour accent2{0xff00D1FF};          // secondary accent
juce::Colour audioWire{0xffE8EDF2};        // audio signal wires
juce::Colour modWire{0xff00D1FF};          // modulation CV wires
juce::Colour pitchWire{0xffAAD4FF};        // poly pitch fan wires
juce::Colour gateWire{0xffFFA500};         // poly gate fan wires
juce::Colour polyBusWire{0xff00E5FF};      // poly ModCV bus wires
juce::Colour textPrimary{0xffEAEEF3};      // primary text
juce::Colour textMuted{0xff8A93A0};        // secondary/label text
juce::Colour textDisabled{0xff5C6470};     // disabled / bypassed text
juce::Colour success{0xff46C66B};          // activity LED / OK
juce::Colour warning{0xffE0A33D};          // warning / mute-pending
juce::Colour error{0xffE5484D};            // error / mute
juce::Colour knobBody{0xff13161B};         // knob body gradient inner stop
juce::Colour knobPointer{0xffEAEEF3};      // knob pointer line
juce::Colour meterFill{0xff00D1FF};        // output meter fill (top of gradient)
juce:: Colour modRingPositive{0xff00E5FF}; // mod ring, positive modulation
juce::Colour modRingNegative{0xffFF6E00};  // mod ring, negative modulation
Correct Casting Pattern to Access Theme Tokens
// Pattern 1: inside a Component method (e.g. paint())
if (auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel())) {
    const auto& colors = lf->getTheme().colors;
    juce::Colour myColor = colors.accent;                      // etc.
   juce::Font font(lf->getTheme().type.label);
}

// Pattern 2: when using namespace alias (common in ModuleComponent.cpp)
using gsynth::theme::GravisynthLookAndFeel;
auto* lf = dynamic_cast<GravisynthLookAndFeel*>(&getLookAndFeel());
if (lf != nullptr) {
    auto themeColors = lf->getTheme().colors;                 // copy
}

// Pattern 3: for Icons only (headless-safe, may return nullptr)
std::unique_ptr<juce::Drawable> icon;
if (lf != nullptr)
    icon = lf->getIcon(gsynth::theme::Icon::SomeIcon);       // safe in tests
Notes on casting:
- Use &getLookAndFeel() — this is the Component's LnF.
- Use gsynth::theme::GravisynthLookAndFeel or alias to reduce verbosity.
- Cast may fail (lf == nullptr) in headless unit tests where the themed LnF isn't installed.
Fallback Pattern When lf Is Null
Components use this pattern (see ModuleComponent.cpp:567–586, ScopeComponent.h:56–65):
// Recommended idiom:
auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel());

if (lf != nullptr) {
    // Use theme tokens
    juce::Colour c = lf->getTheme().colors.accent;
} else {
    // Fallback: either hardcoded colours or JUCE built-ins (e.g. findColour)
    juce::Colour c = juce::Colours::yellow;  // or use a default hex value
}
Key points for fallbacks:
- Headless tests have no themed LnF installed — never assume the cast succeeds.
- ScopeComponent uses its own defaults (0xff2A2F38, 0xff5C6470) which equal existing theme tokens.
- AIChatComponent spinner should migrate to use colors.accent or colors.textPrimary.
- All fallbacks must render at least decently — no crashes, reasonable contrast.