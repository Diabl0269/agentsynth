# Modulation ring fade

The ring round a modulated knob (and the bar beside a fader) fades in over 160 ms when the knob gets its first
modulation and out over 110 ms when the last is removed (80 ms under Reduce Motion, at once off screen or under
Animations Off), through one `FadeAmount` per destination channel (`ModRingFades`, `ModuleComponent::getModRingFades()`).
The fade follows the routing cache on the card's 15 Hz tick, so a second modulation on a ringed knob changes nothing and
a bypassed routing fades its ring too. A ring fading out is drawn from the routing it last saw, since the cache no
longer lists it; a card meeting its first routings (a project opening) shows them at once. The hover widening is
unchanged. The same tick repaints a ring whose drawing changed since the last repaint (`ModRingFades::steps`: a routing's value
moved by 1/2000 or more, a new amount or polarity, the hover or the fade), so an amount drag follows the pointer even when the source is still (a
Macros button that is off sends a steady 0); only the ring's own box repaints (the circle plus its widest stroke; a fader's mod bar), never the knob
or the card. A knob keeps its own picture (`setBufferedToImage`) for as long as it shows a ring, so that repaint draws
the knob from it instead of painting it again; the picture goes when the last ring has faded out. Tests: `Tests/UI/Graph/ModDot/RingRepaintTests.cpp`,
`Tests/UI/Graph/ModDot/ModRingFadeTests.cpp`.
