# Modulation ring fade

The ring round a modulated knob (and the bar beside a fader) fades in over 160 ms when the knob gets its first
modulation and out over 110 ms when the last is removed (80 ms under Reduce Motion, at once off screen or under
Animations Off), through one `FadeAmount` per destination channel (`ModRingFades`, `ModuleComponent::getModRingFades()`).
The fade follows the routing cache on the card's 15 Hz tick, so a second modulation on a ringed knob changes nothing and
a bypassed routing fades its ring too. A ring fading out is drawn from the routing it last saw, since the cache no
longer lists it; a card meeting its first routings (a project opening) shows them at once. The hover widening is
unchanged. Test: `Tests/UI/Graph/ModDot/ModRingFadeTests.cpp`.
