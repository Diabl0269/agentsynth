// SamplerMapBlocks.cpp -- the map's keyboard, rows and blocks: layout, painting, the arrow keys and the file drop.
#include "UI/Graph/ModuleComponent/SamplerChrome/SamplerMapBlocks.h"
#include "Modules/SamplerModule.h"
#include "UI/Layout/DialogKeyboard.h"
#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <algorithm>

namespace synth::ui {

namespace {

constexpr int kLabelWidth = 92;
constexpr int kMargin = 4;
constexpr int kKeysHeight = 46;
constexpr int kKeysGap = 6;
constexpr int kMinRowHeight = 24;
constexpr int kMaxRowHeight = 44;
constexpr int kBlockInset = 2;
constexpr int kMinKeys = 36;
constexpr int kOctave = 12;
constexpr int kDefaultLow = 36;
constexpr float kBlackKeyShare = 0.62f;
constexpr float kSoftDarken = 0.35f;
constexpr float kBlockFontSize = 11.0f;
constexpr float kLabelFontSize = 12.0f;
constexpr int kLabelMinBlockWidth = 28;

bool isBlackKey(int note) {
    const int pitchClass = note % kOctave;
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

juce::Range<int> keySpanFor(const std::vector<synth::sampler::RegionSpec>& specs) {
    if (specs.empty())
        return {kDefaultLow, kDefaultLow + kMinKeys - 1};
    int low = 127, high = 0;
    for (const auto& r : specs) {
        low = std::min(low, r.loKey);
        high = std::max(high, r.hiKey);
    }
    low -= low % kOctave;
    high = std::min(127, high - high % kOctave + kOctave - 1);
    while (high - low + 1 < kMinKeys) {
        if (low >= kOctave)
            low -= kOctave;
        if (high - low + 1 < kMinKeys && high + kOctave <= 127)
            high += kOctave;
        if (low < kOctave && high + kOctave > 127)
            break;
    }
    return {low, high};
}

} // namespace

// One recording on the map. Not a Tab stop: the map is, and its arrow keys walk the blocks.
class SamplerMapBlock : public juce::Button {
public:
    SamplerMapBlock(SamplerMapBlocks& owner, int region, const synth::sampler::RegionSpec& spec, int attackRow)
        : juce::Button(juce::File(spec.samplePath).getFileNameWithoutExtension())
        , owner_(owner)
        , region_(region)
        , row_(attackRow)
        , release_(spec.releaseTrigger) {
        setWantsKeyboardFocus(false);
        setTitle(synth::sampler::describeRegion(spec));
        setTooltip(synth::sampler::describeRegion(spec) + ". Click to edit.");
        onClick = [this] {
            owner_.select(region_);
            owner_.grabKeyboardFocus();
        };
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
        const auto& theme = synth::theme::themeOf(*this);
        const bool selected = owner_.getSelected() == region_;
        auto colour =
            release_ ? theme.colors.warning.darker(kSoftDarken) : theme.colors.accent.darker(kSoftDarken * (float)row_);
        if (selected || highlighted)
            colour = colour.brighter(selected ? 0.2f : 0.1f);
        const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(colour);
        g.fillRoundedRectangle(bounds, theme.metrics.cornerRadiusSmall);
        if (selected) {
            g.setColour(theme.colors.textPrimary);
            g.drawRoundedRectangle(bounds.reduced(1.0f), theme.metrics.cornerRadiusSmall, 2.0f);
        }
        if (getWidth() < kLabelMinBlockWidth)
            return;
        g.setColour(colour.getPerceivedBrightness() > 0.55f ? theme.colors.iconInk : theme.colors.iconPaper);
        g.setFont(juce::Font(juce::FontOptions(kBlockFontSize)));
        g.drawText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centredLeft, true);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        owner_.select(region_);
        if (owner_.onOpenFields)
            owner_.onOpenFields();
    }

private:
    SamplerMapBlocks& owner_;
    int region_;
    int row_;
    bool release_;
};

SamplerMapBlocks::SamplerMapBlocks() {
    setWantsKeyboardFocus(true);
    setTitle("Instrument map");
    setTooltip("Instrument map: the arrow keys move between recordings, Enter edits the selected one, Delete removes "
               "it, Esc closes. Drop a recording on a block to replace it, or on a key to add one.");
}

SamplerMapBlocks::~SamplerMapBlocks() = default;

void SamplerMapBlocks::setRegions(const std::vector<synth::sampler::RegionSpec>& specs, int selected) {
    specs_ = specs;
    layers_ = synth::sampler::mapLayers(specs_);
    span_ = keySpanFor(specs_);
    selected_ = selected >= 0 && selected < (int)specs_.size() ? selected : -1;
    rebuildBlocks();
    updateTitle();
    repaint();
}

void SamplerMapBlocks::rebuildBlocks() {
    blocks_.clear();
    rowOfRegion_.assign(specs_.size(), 0);
    for (size_t row = 0; row < layers_.size(); ++row)
        for (const int index : layers_[row].regions)
            rowOfRegion_[(size_t)index] = (int)row;
    blocks_.resize(specs_.size());
    for (size_t row = 0; row < layers_.size(); ++row)
        for (const int index : layers_[row].regions) {
            blocks_[(size_t)index] = std::make_unique<SamplerMapBlock>(*this, index, specs_[(size_t)index], (int)row);
            addAndMakeVisible(*blocks_[(size_t)index]);
        }
    layoutBlocks();
}

juce::Rectangle<int> SamplerMapBlocks::keysArea() const {
    return {kLabelWidth, 0, juce::jmax(1, getWidth() - kLabelWidth - kMargin), kKeysHeight};
}

juce::Rectangle<int> SamplerMapBlocks::rowsArea() const {
    const int top = kKeysHeight + kKeysGap;
    return {kLabelWidth, top, keysArea().getWidth(), juce::jmax(0, getHeight() - top)};
}

float SamplerMapBlocks::keyWidth() const { return (float)keysArea().getWidth() / (float)(span_.getLength() + 1); }

int SamplerMapBlocks::keyAt(int x) const {
    const auto area = keysArea();
    if (x < area.getX() || x >= area.getRight())
        return -1;
    return juce::jmin(span_.getEnd(), span_.getStart() + (int)((float)(x - area.getX()) / keyWidth()));
}

int SamplerMapBlocks::rowAt(int y) const {
    const auto area = rowsArea();
    if (layers_.empty() || y < area.getY())
        return -1;
    const int rowHeight = juce::jlimit(kMinRowHeight, kMaxRowHeight, area.getHeight() / (int)layers_.size());
    const int row = (y - area.getY()) / rowHeight;
    return row < (int)layers_.size() ? row : -1;
}

// Round-robin variants of one key range share the row, each in its own lane.
juce::Rectangle<int> SamplerMapBlocks::blockBounds(int regionIndex, int row) const {
    const auto& r = specs_[(size_t)regionIndex];
    const auto rows = rowsArea();
    const int rowHeight = juce::jlimit(kMinRowHeight, kMaxRowHeight, rows.getHeight() / (int)layers_.size());
    const float kw = keyWidth();
    const int x = rows.getX() + (int)((float)(r.loKey - span_.getStart()) * kw);
    const int right = rows.getX() + (int)((float)(r.hiKey - span_.getStart() + 1) * kw);
    const int laneCount = juce::jmax(1, r.seqLength);
    const int laneHeight = (rowHeight - 2 * kBlockInset) / laneCount;
    const int lane = (r.seqPosition - 1) % laneCount;
    return {x, rows.getY() + row * rowHeight + kBlockInset + lane * laneHeight, juce::jmax(3, right - x - 1),
            laneHeight};
}

void SamplerMapBlocks::layoutBlocks() {
    if (layers_.empty())
        return;
    for (size_t i = 0; i < blocks_.size(); ++i)
        if (blocks_[i] != nullptr)
            blocks_[i]->setBounds(blockBounds((int)i, rowOfRegion_[i]));
}

void SamplerMapBlocks::resized() { layoutBlocks(); }

juce::Button* SamplerMapBlocks::getBlock(int index) const {
    return index >= 0 && index < (int)blocks_.size() ? blocks_[(size_t)index].get() : nullptr;
}

void SamplerMapBlocks::select(int index) {
    if (index == selected_)
        return;
    selected_ = index;
    updateTitle();
    repaint();
    for (auto& block : blocks_)
        if (block != nullptr)
            block->repaint();
    if (onSelect)
        onSelect(selected_);
}

void SamplerMapBlocks::updateTitle() {
    setTitle(selected_ >= 0 ? "Instrument map, " + synth::sampler::describeRegion(specs_[(size_t)selected_])
                            : juce::String("Instrument map"));
}

// Every semitone gets an equal column so the keys line up with the blocks below. The white keys form one band under
// the black ones, split at each black key's centre and at E-F and B-C, so the octave labels sit on an unbroken strip.
void SamplerMapBlocks::paintKeys(juce::Graphics& g, juce::Rectangle<float> area) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const float kw = keyWidth();
    const float blackHeight = area.getHeight() * kBlackKeyShare;
    g.setColour(colors.pianoKeyWhite);
    g.fillRect(area);
    for (int note = span_.getStart(); note <= span_.getEnd(); ++note) {
        const float x = area.getX() + (float)(note - span_.getStart()) * kw;
        if (isBlackKey(note)) {
            g.setColour(colors.pianoKeyBlack);
            g.fillRect(juce::Rectangle<float>(x, area.getY(), kw, blackHeight));
            g.setColour(colors.border);
            g.fillRect(juce::Rectangle<float>(x + kw * 0.5f - 0.25f, area.getY() + blackHeight, 0.5f,
                                              area.getHeight() - blackHeight));
        } else if (note % kOctave == 0 || note % kOctave == 5) {
            g.setColour(colors.border);
            g.fillRect(juce::Rectangle<float>(x, area.getY(), 0.5f, area.getHeight()));
        }
    }
    g.setColour(colors.pianoKeyWhite.contrasting(0.7f));
    g.setFont(juce::Font(juce::FontOptions(kBlockFontSize - 1.0f)));
    for (int note = span_.getStart(); note <= span_.getEnd(); ++note)
        if (note % kOctave == 0)
            g.drawText(synth::sampler::keyName(note),
                       juce::Rectangle<float>(area.getX() + (float)(note - span_.getStart()) * kw + 2.0f,
                                              area.getBottom() - 13.0f, (float)kOctave * kw - 4.0f, 12.0f),
                       juce::Justification::centredLeft, false);
}

void SamplerMapBlocks::paintRowLabels(juce::Graphics& g) const {
    const auto& theme = synth::theme::themeOf(*this);
    int attackCount = 0;
    for (const auto& layer : layers_)
        attackCount += layer.release ? 0 : 1;
    g.setColour(theme.colors.textMuted);
    g.setFont(juce::Font(juce::FontOptions(kLabelFontSize)));
    const auto rows = rowsArea();
    const int rowHeight = juce::jlimit(kMinRowHeight, kMaxRowHeight, rows.getHeight() / juce::jmax(1, getRowCount()));
    int attackRow = 0;
    for (size_t i = 0; i < layers_.size(); ++i) {
        const auto name = synth::sampler::layerName(layers_[i], attackCount, attackRow);
        attackRow += layers_[i].release ? 0 : 1;
        g.drawText(
            name, juce::Rectangle<int>(kMargin, rows.getY() + (int)i * rowHeight, kLabelWidth - kMargin * 2, rowHeight),
            juce::Justification::centredLeft, true);
    }
}

void SamplerMapBlocks::paint(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    g.setColour(theme.colors.bg1);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), theme.metrics.cornerRadiusSmall);
    paintKeys(g, keysArea().toFloat());
    g.setColour(theme.colors.border);
    const auto rows = rowsArea();
    const int rowHeight = juce::jlimit(kMinRowHeight, kMaxRowHeight, rows.getHeight() / juce::jmax(1, getRowCount()));
    for (int i = 0; i < getRowCount(); ++i)
        g.fillRect(rows.getX(), rows.getY() + (i + 1) * rowHeight - 1, rows.getWidth(), 1);
    paintRowLabels(g);
    if (specs_.empty()) {
        g.setColour(theme.colors.textMuted);
        g.setFont(juce::Font(juce::FontOptions(kLabelFontSize)));
        g.drawText("No recordings yet. Drop audio files on the keyboard.", rows, juce::Justification::centred, false);
    }
}

void SamplerMapBlocks::paintOverChildren(juce::Graphics& g) {
    const auto& theme = synth::theme::themeOf(*this);
    if (dropRegion_ >= 0 && blocks_[(size_t)dropRegion_] != nullptr) {
        g.setColour(theme.colors.accent);
        g.drawRoundedRectangle(blocks_[(size_t)dropRegion_]->getBounds().toFloat().expanded(1.5f),
                               theme.metrics.cornerRadiusSmall, 2.0f);
    } else if (dropKey_ >= 0) {
        const float kw = keyWidth();
        g.setColour(theme.colors.accent.withAlpha(0.3f));
        g.fillRect(juce::Rectangle<float>((float)keysArea().getX() + (float)(dropKey_ - span_.getStart()) * kw, 0.0f,
                                          kw, (float)getHeight()));
    }
    if (!hasKeyboardFocus(false))
        return;
    if (auto* block = getBlock(selected_))
        paintFocusRingAlways(g, block->getBounds().toFloat().expanded(2.5f), *this, theme.metrics.cornerRadiusSmall);
    else
        paintFocusRing(g, getLocalBounds().toFloat(), *this, theme.metrics.cornerRadiusSmall);
}

bool SamplerMapBlocks::keyPressed(const juce::KeyPress& key) {
    using synth::sampler::MapMove;
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;
    if (key == juce::KeyPress::escapeKey)
        return closeHostingWindow(*this);
    if (key == juce::KeyPress::returnKey) {
        if (selected_ >= 0 && onOpenFields)
            onOpenFields();
        return selected_ >= 0;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (selected_ >= 0 && onRemove)
            onRemove();
        return true;
    }
    const std::pair<int, MapMove> arrows[] = {{juce::KeyPress::leftKey, MapMove::Left},
                                              {juce::KeyPress::rightKey, MapMove::Right},
                                              {juce::KeyPress::upKey, MapMove::Up},
                                              {juce::KeyPress::downKey, MapMove::Down}};
    for (const auto& [code, move] : arrows) {
        if (key.getKeyCode() != code)
            continue;
        if (specs_.empty())
            return true;
        select(selected_ < 0 ? layers_.front().regions.front()
                             : synth::sampler::moveSelection(specs_, layers_, selected_, move));
        return true;
    }
    return false;
}

MapDrop SamplerMapBlocks::dropAt(int x, int y, const juce::StringArray& paths) const {
    MapDrop drop;
    for (const auto& path : paths)
        if (SamplerModule::isSupportedAudioFile(juce::File(path)))
            drop.paths.add(path);
    for (size_t i = 0; i < blocks_.size() && drop.replaceRegion < 0; ++i)
        if (blocks_[i] != nullptr && blocks_[i]->getBounds().contains(x, y))
            drop.replaceRegion = (int)i;
    if (drop.replaceRegion >= 0)
        return drop;
    drop.key = keyAt(x);
    if (drop.key < 0 || y < 0 || y >= getHeight()) {
        drop.paths.clear();
        return drop;
    }
    if (const int row = rowAt(y); row >= 0 && !layers_[(size_t)row].release) {
        drop.loVel = layers_[(size_t)row].loVel;
        drop.hiVel = layers_[(size_t)row].hiVel;
    }
    return drop;
}

bool SamplerMapBlocks::isInterestedInFileDrag(const juce::StringArray& files) {
    if (!editable_)
        return false;
    return std::any_of(files.begin(), files.end(),
                       [](const juce::String& f) { return SamplerModule::isSupportedAudioFile(juce::File(f)); });
}

void SamplerMapBlocks::fileDragMove(const juce::StringArray& files, int x, int y) {
    const auto drop = dropAt(x, y, files);
    dropRegion_ = drop.paths.isEmpty() ? -1 : drop.replaceRegion;
    dropKey_ = drop.paths.isEmpty() || drop.replaceRegion >= 0 ? -1 : drop.key;
    repaint();
}

void SamplerMapBlocks::fileDragExit(const juce::StringArray&) {
    dropRegion_ = dropKey_ = -1;
    repaint();
}

void SamplerMapBlocks::filesDropped(const juce::StringArray& files, int x, int y) {
    dropRegion_ = dropKey_ = -1;
    repaint();
    const auto drop = dropAt(x, y, files);
    if (editable_ && !drop.paths.isEmpty() && onDrop)
        onDrop(drop);
}

} // namespace synth::ui
