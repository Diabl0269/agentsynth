# Third-party licences

Code that ships inside the app and is not ours, other than JUCE and Sparkle (their licences travel with their downloads) and the fonts ([assets/fonts/LICENSES.md](../../assets/fonts/LICENSES.md)). A new dependency that ships gets its licence text here in the same change.

## Signalsmith Stretch and Signalsmith Linear

The sound browser's loop stretching ([Loops match the song](../layout/sound-browser.md#loops-match-the-song)) uses both libraries, header-only, from `FetchContent` pins in `cmake/DependencyVersions.cmake` (Stretch 1.4.0, Linear 0.6.4). Both are under the MIT licence.

```
MIT License

Copyright (c) 2022 Geraint Luff / Signalsmith Audio Ltd.   (Signalsmith Stretch)
Copyright (c) 2025 Signalsmith Audio                       (Signalsmith Linear)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
