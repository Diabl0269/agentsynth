# Depth change cross-fade

Changing the Depth level ([theming](theming.md#depth-levels)) re-skins every surface at once, so it cross-fades instead
of popping. In `MainComponent::changeListenerCallback`'s theme branch, when the new active theme's depth differs from the
one the look and feel holds, the window is pictured first, then re-skinned, then the picture (a `FadeOutPicture`, id
`depthFadePicture`, laid over the whole window) fades out: 200 ms, the shared 80 ms plain fade under Reduce Motion, and
no picture at all with Animations Off. The picture takes no mouse, is hidden from accessibility and releases its image
when the fade ends, so the window is static at idle. A plain theme switch leaves the depth alone and shows no picture.
A detached panel window is not covered by the picture. `FadeVisibility::setFadeOutMs` and
`FadeOutPicture::play(ms)` / `playAt(bounds, ms)` take the length.

See [theming](theming.md#depth-levels) for the setting itself and [animation](animation.md#fading-things-in-and-out) for the shared fade.
