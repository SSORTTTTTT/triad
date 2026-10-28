#include "Look.h"
#include "Theme.h"

namespace triad::ui
{
// ---- Icons -----------------------------------------------------------------------------------------
IconButton::IconButton (Icon i, const juce::String& tip) : Button (tip), icon (i)
{
    setTooltip (tip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? currentPalette.controlsOn : over ? currentPalette.iconOver : currentPalette.iconFill);
    g.fillRoundedRectangle (r, 4.0f);
    const auto c = ! isEnabled() ? dimTextColour.withAlpha (0.35f) : getToggleState() ? accent : textColour.withAlpha (over ? 1.0f : 0.85f);
    drawIcon (g, icon, r.reduced (r.getHeight() * 0.24f), c);
}
void IconButton::drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> b, juce::Colour c)
{
    g.setColour (c);
    juce::Path p;
    const auto x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    const juce::PathStrokeType stroke (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (icon)
    {
        case Icon::trash:
            p.addRectangle (x + w * 0.22f, y + h * 0.28f, w * 0.56f, h * 0.7f);
            p.startNewSubPath (x + w * 0.1f, y + h * 0.2f); p.lineTo (x + w * 0.9f, y + h * 0.2f);
            p.startNewSubPath (x + w * 0.38f, y + h * 0.08f); p.lineTo (x + w * 0.62f, y + h * 0.08f);
            p.startNewSubPath (x + w * 0.42f, y + h * 0.42f); p.lineTo (x + w * 0.42f, y + h * 0.84f);
            p.startNewSubPath (x + w * 0.58f, y + h * 0.42f); p.lineTo (x + w * 0.58f, y + h * 0.84f);
            g.strokePath (p, stroke); return;
        case Icon::undo: case Icon::redo:
        {
            const bool redo = icon == Icon::redo;
            p.addCentredArc (x + w * 0.5f, y + h * 0.58f, w * 0.36f, h * 0.34f, 0.0f, redo ? -2.2f : 2.2f, redo ? 1.2f : -1.2f, true);
            g.strokePath (p, stroke);
            juce::Path head;
            const float hx = redo ? x + w * 0.82f : x + w * 0.18f, hy = y + h * 0.38f;
            head.addTriangle (hx, hy - h * 0.2f, hx + (redo ? -w * 0.02f : w * 0.02f), hy + h * 0.14f, hx + (redo ? w * 0.2f : -w * 0.2f), hy);
            g.fillPath (head); return;
        }
        case Icon::reverse:
            p.startNewSubPath (x, y + h * 0.3f); p.lineTo (x + w, y + h * 0.3f);
            p.startNewSubPath (x, y + h * 0.7f); p.lineTo (x + w, y + h * 0.7f);
            g.strokePath (p, stroke);
            p.clear();
            p.addTriangle (x + w, y + h * 0.3f, x + w * 0.72f, y + h * 0.1f, x + w * 0.72f, y + h * 0.5f);
            p.addTriangle (x, y + h * 0.7f, x + w * 0.28f, y + h * 0.5f, x + w * 0.28f, y + h * 0.9f);
            g.fillPath (p); return;
        case Icon::flipVertical:
            p.startNewSubPath (x + w * 0.3f, y); p.lineTo (x + w * 0.3f, y + h);
            p.startNewSubPath (x + w * 0.7f, y); p.lineTo (x + w * 0.7f, y + h);
            g.strokePath (p, stroke);
            p.clear();
            p.addTriangle (x + w * 0.3f, y, x + w * 0.1f, y + h * 0.28f, x + w * 0.5f, y + h * 0.28f);
            p.addTriangle (x + w * 0.7f, y + h, x + w * 0.5f, y + h * 0.72f, x + w * 0.9f, y + h * 0.72f);
            g.fillPath (p); return;
        case Icon::left:
            p.addTriangle (x + w * 0.7f, y + h * 0.15f, x + w * 0.7f, y + h * 0.85f, x + w * 0.25f, y + h * 0.5f);
            g.fillPath (p); return;
        case Icon::right:
            p.addTriangle (x + w * 0.3f, y + h * 0.15f, x + w * 0.3f, y + h * 0.85f, x + w * 0.75f, y + h * 0.5f);
            g.fillPath (p); return;
        case Icon::moveLeft:    // move the wave: an arrow head and its bar (ShaperBox's move buttons)
            p.addTriangle (x + w * 0.05f, y + h * 0.5f, x + w * 0.45f, y + h * 0.18f, x + w * 0.45f, y + h * 0.82f);
            p.addRectangle (x + w * 0.42f, y + h * 0.4f, w * 0.48f, h * 0.2f);
            g.fillPath (p); return;
        case Icon::moveRight:
            p.addTriangle (x + w * 0.95f, y + h * 0.5f, x + w * 0.55f, y + h * 0.18f, x + w * 0.55f, y + h * 0.82f);
            p.addRectangle (x + w * 0.1f, y + h * 0.4f, w * 0.48f, h * 0.2f);
            g.fillPath (p); return;
        case Icon::gear:
        {
            const auto cx = x + w * 0.5f, cy = y + h * 0.5f, r = juce::jmin (w, h) * 0.5f;
            for (int i = 0; i < 8; ++i)
            {
                const auto a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                juce::Path tooth;
                tooth.addRectangle (-r * 0.16f, -r, r * 0.32f, r * 0.4f);
                g.fillPath (tooth, juce::AffineTransform::rotation (a).translated (cx, cy));
            }
            g.drawEllipse (cx - r * 0.66f, cy - r * 0.66f, r * 1.32f, r * 1.32f, 2.2f);
            g.fillEllipse (cx - r * 0.2f, cy - r * 0.2f, r * 0.4f, r * 0.4f);
            return;
        }
        case Icon::menu:
            for (float f : { 0.2f, 0.5f, 0.8f }) { p.startNewSubPath (x, y + h * f); p.lineTo (x + w, y + h * f); }
            g.strokePath (p, stroke); return;
        case Icon::plus:
            p.startNewSubPath (x + w * 0.5f, y); p.lineTo (x + w * 0.5f, y + h);
            p.startNewSubPath (x, y + h * 0.5f); p.lineTo (x + w, y + h * 0.5f);
            g.strokePath (p, stroke); return;
        case Icon::close:
            p.startNewSubPath (x + w * 0.15f, y + h * 0.15f); p.lineTo (x + w * 0.85f, y + h * 0.85f);
            p.startNewSubPath (x + w * 0.85f, y + h * 0.15f); p.lineTo (x + w * 0.15f, y + h * 0.85f);
            g.strokePath (p, stroke); return;
        case Icon::lfo:
            p.startNewSubPath (x, y + h * 0.5f);
            for (int i = 1; i <= 24; ++i)
            {
                const float t = (float) i / 24.0f;
                p.lineTo (x + w * t, y + h * (0.5f - 0.42f * std::sin (t * juce::MathConstants<float>::twoPi)));
            }
            g.strokePath (p, juce::PathStrokeType (1.5f)); return;
        case Icon::dice:
        {
            const auto box = juce::Rectangle<float> (x, y, w, h).withSizeKeepingCentre (juce::jmin (w, h), juce::jmin (w, h));
            p.addRoundedRectangle (box, box.getWidth() * 0.2f);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            const float d = box.getWidth() * 0.17f;
            for (const auto& f : { juce::Point<float> (0.28f, 0.28f), juce::Point<float> (0.72f, 0.28f), juce::Point<float> (0.5f, 0.5f),
                                   juce::Point<float> (0.28f, 0.72f), juce::Point<float> (0.72f, 0.72f) })
                g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ box.getX() + box.getWidth() * f.x, box.getY() + box.getHeight() * f.y }));
            return;
        }
    }
}

// ---- Light ------------------------------------------------------------------------------------------
Led::Led (const juce::String& tip) : Button (tip)
{
    setTooltip (tip);
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void Led::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto light = getLocalBounds().toFloat().withSizeKeepingCentre (9.0f, 9.0f);
    if (getToggleState())
    {
        g.setColour (ledColour.withAlpha (over ? 0.35f : 0.25f));
        g.fillEllipse (light.expanded (2.5f));
        g.setColour (ledColour);
    }
    else g.setColour (over ? currentPalette.ledOffOver : currentPalette.ledOff);
    g.fillEllipse (light);
}

// ---- Look ---------------------------------------------------------------------------------------------
namespace
{
std::vector<TriadLook*>& liveLooks() { static std::vector<TriadLook*> looks; return looks; }
}
TriadLook::TriadLook()
{
    syncThemeFromSettings();
    applyTheme();
    liveLooks().push_back (this);
    juce::LookAndFeel::setDefaultLookAndFeel (this);
}
TriadLook::~TriadLook()
{
    auto& looks = liveLooks();
    looks.erase (std::remove (looks.begin(), looks.end(), this), looks.end());
    juce::LookAndFeel::setDefaultLookAndFeel (looks.empty() ? nullptr : looks.back());
}
void TriadLook::applyTheme()
{
    const auto& p = currentPalette;
    // The default theme's buttons were a shade darker than its slot rows: kept as they were.
    setColour (juce::TextButton::buttonColourId, p.controls == juce::Colour (0xff2e3230) ? juce::Colour (0xff2c302e) : p.controls);
    setColour (juce::TextButton::buttonOnColourId, p.controlsOn);
    setColour (juce::TextButton::textColourOffId, p.text);
    setColour (juce::TextButton::textColourOnId, p.accent);
    setColour (juce::Label::textColourId, p.dimText);
    // Menus (0.16.0): the panel's colour is not quite opaque, so the menu's window is see-through and its rounded
    // corners show what is behind them (drawPopupMenuBackgroundWithOptions draws the panel itself, opaque).
    const auto m = menuColours();
    setColour (juce::PopupMenu::backgroundColourId, m.back.withAlpha ((juce::uint8) 254));
    setColour (juce::PopupMenu::textColourId, m.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, m.hover);
    setColour (juce::PopupMenu::highlightedTextColourId, m.text);
    setColour (juce::PopupMenu::headerTextColourId, m.header);
    setColour (juce::TooltipWindow::backgroundColourId, m.back);
    setColour (juce::TooltipWindow::textColourId, m.text);
    setColour (juce::TooltipWindow::outlineColourId, m.edge);
    setColour (juce::ScrollBar::thumbColourId, p.scrollThumb);
    setColour (juce::TextEditor::backgroundColourId, p.field);
    setColour (juce::TextEditor::outlineColourId, p.fieldOutline);
    setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    setColour (juce::TextEditor::textColourId, p.text);
    setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, p.text);
    setColour (juce::ComboBox::backgroundColourId, p.controls);
    setColour (juce::ComboBox::textColourId, p.text);
    setColour (juce::ComboBox::outlineColourId, p.fieldOutline);
    setColour (juce::ComboBox::arrowColourId, p.dimText);
    setColour (juce::Slider::textBoxTextColourId, p.text);
    setColour (juce::Slider::textBoxBackgroundColourId, p.field);
    setColour (juce::Slider::textBoxOutlineColourId, p.fieldOutline);
    setColour (juce::Slider::thumbColourId, p.sliderThumb);
    setColour (juce::Slider::trackColourId, p.accent);
    setColour (juce::Slider::backgroundColourId, p.sliderTrack);
    setColour (juce::ResizableWindow::backgroundColourId, p.background);
    setColour (juce::DocumentWindow::textColourId, p.text);
}
// ---- Value boxes ----------------------------------------------------------------------------------------
juce::Rectangle<float> valueBoxArea (juce::Rectangle<float> b, bool captioned)
{
    // As tall as it needs (a knob's square area keeps its label above, the box at the top of it).
    if (captioned) return b;
    return b.withHeight (juce::jmin (b.getHeight(), juce::jmax (22.0f, b.getWidth() * 0.62f)));
}
juce::Rectangle<float> valueBoxMeter (juce::Rectangle<float> b, bool captioned)
{
    const auto box = valueBoxArea (b, captioned);
    const bool small = box.getHeight() < 34.0f, short_ = box.getHeight() < 44.0f;
    const float inset = small || box.getWidth() < 70.0f ? 6.0f : 10.0f, thick = short_ ? 3.0f : 4.0f, fromBottom = small ? 4.0f : short_ ? 5.0f : 9.0f;
    return { box.getX() + inset, box.getBottom() - fromBottom - thick, box.getWidth() - 2.0f * inset, thick };
}
void TriadLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& slider)
{
    // A box: the value on top, a line meter under it filling left to right in the knob's colour (0.16.0).
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto caption = slider.getProperties().getWithDefault ("triadLabel", {}).toString();
    const bool captioned = caption.isNotEmpty();
    const auto box = valueBoxArea (bounds, captioned).reduced (0.5f);
    const bool small = box.getHeight() < 34.0f;
    const float alpha = slider.isEnabled() ? 1.0f : 0.5f;
    const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);
    g.setColour (currentPalette.knobBody);
    g.fillRoundedRectangle (box, small ? 5.0f : 7.0f);
    const auto meter = valueBoxMeter (bounds, captioned);
    g.setColour (currentPalette.knobTrack);
    g.fillRoundedRectangle (meter, meter.getHeight() * 0.5f);
    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (meter.withWidth (juce::jmax (meter.getHeight(), meter.getWidth() * juce::jlimit (0.0f, 1.0f, pos))), meter.getHeight() * 0.5f);
    // The value: a percentage, or milliseconds for Triad Time's smoothing, attack and release.
    const auto v = slider.getValue();
    const auto unit = slider.getProperties().getWithDefault ("triadUnit", "%").toString();
    const auto label = unit == "ms" ? (v < 10.0 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + (small ? "" : " ms")
                     : unit == "raw" ? juce::String (juce::roundToInt (v))
                                     : (v >= 99.95 ? juce::String ("100") : juce::String (juce::roundToInt (v))) + "%";
    auto valueArea = box.withBottom (meter.getY() - (small ? 0.0f : 2.0f));
    if (captioned && box.getHeight() < 56.0f && box.getWidth() < 90.0f)
    {
        // A narrow box (Triad Time's MIX AND SMOOTHING): the caption small at the top, the value under it.
        auto inner = valueArea.withTrimmedTop (3.0f);
        g.setColour (dimTextColour.withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold).withKerningFactor (0.05f));
        g.drawText (caption, inner.removeFromTop (11.0f), juce::Justification::centred);
        g.setColour (textColour.withMultipliedAlpha (slider.isEnabled() ? 0.97f : 0.5f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        const auto withUnit = unit == "ms" && small ? label + " ms" : label;   // the unit, even in a narrow box
        g.drawFittedText (withUnit, inner.toNearestInt(), juce::Justification::centred, 1, 0.75f);
        return;
    }
    if (captioned && box.getHeight() < 56.0f)
    {
        // A short box: the caption on the left, the value on the right, the meter under both (RED 255).
        const auto line = valueArea.reduced (10.0f, 0.0f).withTrimmedTop (3.0f);
        g.setColour (dimTextColour.withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.06f));
        g.drawText (caption, line, juce::Justification::centredLeft);
        g.setColour (textColour.withMultipliedAlpha (slider.isEnabled() ? 0.97f : 0.5f));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (label, line, juce::Justification::centredRight);
        return;
    }
    if (captioned)
    {
        g.setColour (dimTextColour.withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold).withKerningFactor (0.06f));
        g.drawText (caption, valueArea.removeFromTop (22.0f).withTrimmedTop (6.0f), juce::Justification::centredTop);
    }
    g.setColour (textColour.withMultipliedAlpha (slider.isEnabled() ? 0.97f : 0.5f));
    g.setFont (juce::FontOptions (small ? juce::jlimit (9.5f, 11.5f, box.getHeight() * 0.44f)
                                        : juce::jlimit (12.0f, 26.0f, valueArea.getHeight() * 0.62f), juce::Font::bold));
    g.drawFittedText (label, valueArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}
void TriadLook::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                  juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, slider);
        return;
    }
    // A Serum-style bar: dark track, coloured fill, rounded thumb.
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
    g.setColour (currentPalette.sliderTrack); g.fillRoundedRectangle (track, 2.0f);
    const auto colour = slider.findColour (juce::Slider::trackColourId);
    g.setColour (colour.withAlpha (slider.isEnabled() ? 0.9f : 0.4f));
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    const auto thumb = juce::Rectangle<float> (14.0f, juce::jmin (14.0f, r.getHeight())).withCentre ({ pos, r.getCentreY() });
    g.setColour (currentPalette.sliderThumb); g.fillRoundedRectangle (thumb, 4.0f);
    g.setColour (currentPalette.sliderThumbLine); g.drawVerticalLine (juce::roundToInt (pos), thumb.getY() + 3.0f, thumb.getBottom() - 3.0f);
}
void TriadLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& fill, bool over, bool down)
{
    auto colour = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : fill;   // the button's own, else the look's
    if (down) colour = colour.brighter (0.15f); else if (over) colour = colour.brighter (0.07f);
    if (! b.isEnabled()) colour = colour.withMultipliedAlpha (0.5f);
    g.setColour (colour);
    g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (0.5f), 4.0f);
}
juce::Font TriadLook::getTextButtonFont (juce::TextButton&, int height)
{
    return juce::FontOptions (juce::jmin (12.5f, (float) height * 0.55f), juce::Font::bold);
}
void TriadLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto align = b.getProperties().getWithDefault ("triadAlign", {}).toString();
    if (align != "left" && align != "arrow" && align != "centre")
    {
        LookAndFeel_V4::drawButtonText (g, b, over, down);
        return;
    }
    // "arrow": centred in what the menu arrow on the right leaves (the matrix's SOURCE and DESTINATION); "centre": a
    // small button's word at its own width (INV), not squeezed.
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    const bool left = align == "left", arrow = align == "arrow";
    g.setFont (juce::FontOptions (left ? 12.0f : juce::jmin (12.0f, (float) b.getHeight() * 0.5f), juce::Font::bold));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (left ? 2 : arrow ? 4 : 1).withTrimmedRight (left ? 16 : arrow ? 13 : 1),
                      left ? juce::Justification::centredLeft : juce::Justification::centred, 1, 0.85f);
}
// ---- Menus ----------------------------------------------------------------------------------------------
namespace
{
const juce::juce_wchar swatchMark = 1, tagMark = 2, lfoMark = 3, filterMark = 4;
bool isTitle (const juce::String& header) { return header.containsAnyOf ("abcdefghijklmnopqrstuvwxyz"); }
juce::Font itemFont() { return juce::FontOptions (12.5f, juce::Font::bold); }
juce::Font headerFont() { return juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.06f); }
juce::Font titleFont() { return juce::FontOptions (14.5f, juce::Font::bold); }
juce::Font sideFont() { return juce::FontOptions (11.0f, juce::Font::bold); }
juce::Font tagFont() { return juce::FontOptions (8.5f, juce::Font::bold).withKerningFactor (0.04f); }
float widthOf (const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }
juce::String fullHeart() { return u8 ("\xe2\x99\xa5 "); }
juce::String emptyHeart() { return u8 ("\xe2\x99\xa1 "); }
}
MenuColours menuColours()
{
    const auto& p = currentPalette;
    MenuColours m;
    m.back = p.controls.interpolatedWith (p.panel, 0.25f);
    // The chosen item is filled with the theme's second colour (Plum's lavender), or its accent if that is too
    // close to the menu itself.
    m.chosen = p.timeLine;
    if (std::abs (m.chosen.getPerceivedBrightness() - m.back.getPerceivedBrightness()) < 0.2f) m.chosen = p.accent;
    m.chosenInk = m.chosen.getPerceivedBrightness() > 0.42f ? p.grid.interpolatedWith (juce::Colours::black, 0.3f) : p.text;
    m.edge = m.chosen.withAlpha (0.38f);
    m.hover = m.chosen.withAlpha (0.2f);
    m.text = p.text;
    m.dim = p.dimText;
    m.header = p.accent;
    m.line = p.text.withAlpha (0.1f);
    return m;
}
juce::String menuSwatches (const juce::Array<juce::Colour>& colours)
{
    auto s = juce::String::charToString (swatchMark);
    for (auto c : colours) s << c.toDisplayString (false) << ",";
    return s;
}
juce::String menuLfo (const juce::String& text) { return juce::String::charToString (lfoMark) + text; }
juce::String menuFilter (char kind, const juce::String& text) { return juce::String::charToString (filterMark) + juce::String::charToString ((juce::juce_wchar) kind) + text; }
void drawFilterIcon (juce::Graphics& g, juce::Rectangle<float> r, char kind, juce::Colour colour)
{
    // s: a band moving sideways; w: a band opening outwards; l: a low cut (high-pass); h: a high cut (low-pass).
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight() - 4.0f, base = y + h, top = y + 1.5f;
    juce::Path curve;
    if (kind == 'l') { curve.startNewSubPath (x, base); curve.cubicTo (x + w * 0.35f, base, x + w * 0.35f, top, x + w * 0.6f, top); curve.lineTo (x + w, top); }
    else if (kind == 'h') { curve.startNewSubPath (x, top); curve.lineTo (x + w * 0.4f, top); curve.cubicTo (x + w * 0.65f, top, x + w * 0.65f, base, x + w, base); }
    else
    {
        const float a = kind == 'w' ? 0.12f : 0.25f, b = kind == 'w' ? 0.88f : 0.75f;
        curve.startNewSubPath (x, base);
        curve.cubicTo (x + w * (a + 0.08f), base, x + w * a, top, x + w * (a + 0.14f), top);
        curve.lineTo (x + w * (b - 0.14f), top);
        curve.cubicTo (x + w * b, top, x + w * (b - 0.08f), base, x + w, base);
    }
    g.setColour (colour.withMultipliedAlpha (0.25f));
    juce::Path fill (curve);
    fill.lineTo (x + w, base); fill.lineTo (x, base); fill.closeSubPath();
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (1.5f));
    // The arrows: which way it moves.
    const float ay = y + r.getHeight() - 1.0f;
    auto arrow = [&] (float from, float to)
    {
        g.drawLine (from, ay, to, ay, 1.2f);
        const float d = to > from ? -3.0f : 3.0f;
        juce::Path head; head.addTriangle (to, ay, to + d, ay - 2.2f, to + d, ay + 2.2f);
        g.fillPath (head);
    };
    if (kind == 's') { arrow (x + w * 0.5f, x + w * 0.85f); arrow (x + w * 0.5f, x + w * 0.15f); }
    else if (kind == 'w') { arrow (x + w * 0.42f, x + w * 0.05f); arrow (x + w * 0.58f, x + w * 0.95f); }
    else if (kind == 'l') { arrow (x + w * 0.28f, x + w * 0.05f); arrow (x + w * 0.28f, x + w * 0.55f); }
    else { arrow (x + w * 0.72f, x + w * 0.45f); arrow (x + w * 0.72f, x + w * 0.95f); }
}
juce::String menuTag (const juce::String& tag) { return juce::String::charToString (tagMark) + tag; }
void addItemWithRight (juce::PopupMenu& menu, int id, const juce::String& name, const juce::String& right, bool enabled, bool ticked)
{
    juce::PopupMenu::Item item (name);
    item.itemID = id;
    item.shortcutKeyDescription = right;
    item.isEnabled = enabled;
    item.isTicked = ticked;
    menu.addItem (std::move (item));
}
void addSubMenu (juce::PopupMenu& menu, const juce::String& name, juce::PopupMenu sub, const juce::String& right, bool enabled, bool ticked)
{
    juce::PopupMenu::Item item (name);
    item.subMenu = std::make_unique<juce::PopupMenu> (std::move (sub));
    item.shortcutKeyDescription = right;
    item.isEnabled = enabled;
    item.isTicked = ticked;
    menu.addItem (std::move (item));
}

MenuChips::MenuChips (juce::StringArray l, int c, std::shared_ptr<int> p)
    : CustomComponent (false), labels (std::move (l)), chosen (c), picked (std::move (p))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void MenuChips::getIdealSize (int& width, int& height)
{
    width = 58 * labels.size() + 22;
    height = 30;
}
juce::Rectangle<float> MenuChips::chip (int index) const
{
    const auto area = getLocalBounds().toFloat().reduced (8.0f, 5.0f);
    const float gap = 6.0f, w = (area.getWidth() - gap * (float) (labels.size() - 1)) / (float) juce::jmax (1, labels.size());
    return { area.getX() + (float) index * (w + gap), area.getY(), w, area.getHeight() };
}
int MenuChips::chipAt (juce::Point<float> p) const
{
    for (int i = 0; i < labels.size(); ++i)
        if (chip (i).contains (p)) return i;
    return -1;
}
void MenuChips::paint (juce::Graphics& g)
{
    const auto m = menuColours();
    const int over = isMouseOver() ? chipAt (getMouseXYRelative().toFloat()) : -1;
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto r = chip (i);
        g.setColour (i == chosen ? m.chosen : i == over ? currentPalette.controls.interpolatedWith (m.chosen, 0.3f) : currentPalette.controls);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (i == chosen ? m.chosenInk : m.text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (labels[i], r, juce::Justification::centred);
    }
}
void MenuChips::mouseUp (const juce::MouseEvent& e)
{
    const int i = chipAt (e.position);
    if (i < 0) return;
    if (picked != nullptr) *picked = i;
    triggerMenuItem();
}

juce::Font TriadLook::getPopupMenuFont() { return itemFont(); }
juce::Component* TriadLook::getParentComponentForMenuOptions (const juce::PopupMenu::Options& options)
{
    if (menusInsideWindow)
        if (auto* target = options.getTargetComponent()) return target->getTopLevelComponent();
    return LookAndFeel_V4::getParentComponentForMenuOptions (options);
}
void TriadLook::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height, const juce::PopupMenu::Options&)
{
    const auto m = menuColours();
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    if (! juce::Desktop::canUseSemiTransparentWindows()) g.fillAll (m.back);   // a square window: no corners to see through
    g.setColour (m.back);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (m.edge);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);
}
void TriadLook::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool isHighlighted,
                                              const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&)
{
    const auto m = menuColours();
    if (item.isSeparator)
    {
        g.setColour (m.line);
        g.fillRect (juce::Rectangle<float> ((float) area.getX() + 12.0f, (float) area.getCentreY() - 0.5f, (float) area.getWidth() - 24.0f, 1.0f));
        return;
    }
    const auto row = area.toFloat().reduced (5.0f, 1.0f);
    const bool ticked = item.isTicked, enabled = item.isEnabled;
    if (ticked)
    {
        g.setColour (isHighlighted ? m.chosen.brighter (0.1f) : m.chosen);
        g.fillRoundedRectangle (row, 6.0f);
    }
    else if (isHighlighted && enabled)
    {
        g.setColour (m.hover);
        g.fillRoundedRectangle (row, 6.0f);
    }
    auto ink = ticked ? m.chosenInk : (item.colour.isTransparent() ? m.text : item.colour);
    if (! enabled) ink = ink.withMultipliedAlpha (0.42f);
    float x = (float) area.getX() + 16.0f;
    const float cy = row.getCentreY();
    if (item.image != nullptr)
    {
        item.image->drawWithin (g, { x, cy - 7.0f, 14.0f, 14.0f }, juce::RectanglePlacement::centred, enabled ? 1.0f : 0.4f);
        x += 20.0f;
    }
    if (ticked)
    {
        juce::Path tick;
        tick.startNewSubPath (x, cy);
        tick.lineTo (x + 3.5f, cy + 3.5f);
        tick.lineTo (x + 10.0f, cy - 4.0f);
        g.setColour (ink);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        x += 17.0f;
    }
    auto text = item.text;
    if (text.startsWith (fullHeart()) || text.startsWith (emptyHeart()))
    {
        drawHeart (g, { x, cy - 6.0f, 13.0f, 12.0f }, text.startsWith (fullHeart()), isHighlighted, ink);
        text = text.substring (2);
        x += 19.0f;
    }
    if (text[0] == filterMark)
    {
        // What the LFO does to the band, as a small filter picture (design, as Ableton's Auto Filter shows its shapes).
        drawFilterIcon (g, { x, cy - 7.0f, 26.0f, 14.0f }, (char) text[1], ticked ? ink : m.header.withMultipliedAlpha (enabled ? 1.0f : 0.45f));
        text = text.substring (2);
        x += 33.0f;
    }
    if (text[0] == lfoMark)
    {
        // The LFO's wave, as on a slot's ~ button (0.18.0: "~ Link to LFO").
        IconButton::drawIcon (g, IconButton::Icon::lfo, { x, cy - 5.0f, 16.0f, 10.0f }, ticked ? ink : m.header.withMultipliedAlpha (enabled ? 1.0f : 0.45f));
        text = text.substring (1);
        x += 23.0f;
    }
    float right = (float) area.getRight() - 14.0f;
    if (item.subMenu != nullptr)
    {
        juce::Path arrow;
        arrow.addTriangle (right - 5.0f, cy - 4.0f, right - 5.0f, cy + 4.0f, right, cy);
        g.setColour (ticked ? m.chosenInk : m.dim.withMultipliedAlpha (enabled ? 1.0f : 0.5f));
        g.fillPath (arrow);
        right -= 13.0f;
    }
    if (const auto side = item.shortcutKeyDescription; side.isNotEmpty())
    {
        if (side[0] == swatchMark)
        {
            // A theme's colours, as small squares.
            auto codes = juce::StringArray::fromTokens (side.substring (1), ",", {});
            codes.removeEmptyStrings();
            float sx = right - (float) codes.size() * 14.0f + 3.0f;
            right = sx - 8.0f;
            for (const auto& code : codes)
            {
                const auto square = juce::Rectangle<float> (sx, cy - 5.5f, 11.0f, 11.0f);
                g.setColour (juce::Colour::fromString ("ff" + code));
                g.fillRoundedRectangle (square, 2.0f);
                g.setColour ((ticked ? m.chosenInk : m.text).withAlpha (0.35f));
                g.drawRoundedRectangle (square.reduced (0.5f), 2.0f, 1.0f);
                sx += 14.0f;
            }
        }
        else if (side[0] == tagMark)
        {
            // VST3 / AU / TRIAD in a small frame.
            const auto tag = side.substring (1);
            const float w = widthOf (tagFont(), tag) + 12.0f;
            const auto frame = juce::Rectangle<float> (right - w, cy - 7.5f, w, 15.0f);
            g.setColour ((ticked ? m.chosenInk : m.edge).withMultipliedAlpha (ticked ? 0.7f : 1.0f));
            g.drawRoundedRectangle (frame.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (ticked ? m.chosenInk : m.dim);
            g.setFont (tagFont());
            g.drawText (tag, frame, juce::Justification::centred);
            right = frame.getX() - 8.0f;
        }
        else
        {
            g.setColour ((ticked ? m.chosenInk : m.dim).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
            g.setFont (sideFont());
            const float w = widthOf (sideFont(), side);
            g.drawText (side, juce::Rectangle<float> (right - w - 1.0f, row.getY(), w + 2.0f, row.getHeight()), juce::Justification::centredRight);
            right -= w + 10.0f;
        }
    }
    g.setColour (ink);
    g.setFont (itemFont());
    g.drawFittedText (text, juce::Rectangle<float> (x, row.getY(), juce::jmax (10.0f, right - x), row.getHeight()).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.9f);
}
void TriadLook::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& text,
                                                       const juce::PopupMenu::Options&)
{
    const auto m = menuColours();
    if (isTitle (text))
    {
        // A name (a plug-in, a preset, "Band 2 / Slot 1"): larger, in the text colour.
        g.setColour (m.text);
        g.setFont (titleFont());
        g.drawFittedText (text, area.getX() + 14, area.getY() + 2, area.getWidth() - 24, area.getHeight() - 4, juce::Justification::centredLeft, 1);
        return;
    }
    g.setColour (m.header);
    g.setFont (headerFont());
    if (text[0] == lfoMark)
    {
        // A section about an LFO: its wave first.
        IconButton::drawIcon (g, IconButton::Icon::lfo, { (float) area.getX() + 14.0f, (float) area.getCentreY() - 3.0f, 15.0f, 9.0f }, m.header);
        g.drawFittedText (text.substring (1), area.getX() + 35, area.getY() + 5, area.getWidth() - 43, area.getHeight() - 7, juce::Justification::centredLeft, 1);
        return;
    }
    g.drawFittedText (text, area.getX() + 14, area.getY() + 5, area.getWidth() - 22, area.getHeight() - 7, juce::Justification::centredLeft, 1);
}
void TriadLook::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight,
                                                      const juce::PopupMenu::Options&)
{
    if (isSeparator) { idealWidth = 40; idealHeight = menuSeparatorHeight; return; }
    idealHeight = menuRowHeight;
    // The text arrives with what is on the right after it ("name   right"): squares and tags are measured as drawn.
    auto name = text;
    float extra = 0.0f;
    if (const int i = text.indexOfChar (swatchMark); i >= 0)
    {
        extra = (float) text.substring (i).retainCharacters (",").length() * 14.0f + 12.0f;
        name = text.substring (0, i).trimEnd();
    }
    else if (const int j = text.indexOfChar (tagMark); j >= 0)
    {
        extra = widthOf (tagFont(), text.substring (j + 1)) + 24.0f;
        name = text.substring (0, j).trimEnd();
    }
    if (name.startsWith (fullHeart()) || name.startsWith (emptyHeart())) { name = name.substring (2); extra += 19.0f; }
    if (name[0] == lfoMark) { name = name.substring (1); extra += 23.0f; }
    if (name[0] == filterMark) { name = name.substring (2); extra += 33.0f; }
    idealWidth = juce::jmax (120, juce::roundToInt (16.0f + 17.0f + widthOf (itemFont(), name) + extra + 40.0f));
}
void TriadLook::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int, int& idealWidth, int& idealHeight,
                                                               const juce::PopupMenu::Options&)
{
    const bool title = isTitle (text);
    idealHeight = title ? menuTitleHeight : menuHeaderHeight;
    idealWidth = juce::roundToInt (widthOf (title ? titleFont() : headerFont(), text) * (title ? 1.0f : 1.08f) + 40.0f);
}
void TriadLook::drawPopupMenuColumnSeparatorWithOptions (juce::Graphics& g, const juce::Rectangle<int>& bounds, const juce::PopupMenu::Options&)
{
    g.setColour (menuColours().line);
    g.fillRect (bounds.toFloat().withSizeKeepingCentre (1.0f, juce::jmax (0.0f, (float) bounds.getHeight() - 20.0f)));
}

// ---- Tooltips and the START FROM box, in the menus' look -----------------------------------------------------
namespace
{
juce::TextLayout tooltipLayout (const juce::String& text, juce::Colour colour)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, juce::FontOptions (12.0f, juce::Font::bold), colour);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 300.0f);
    return layout;
}
}
juce::Rectangle<int> TriadLook::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    const auto layout = tooltipLayout (tipText, textColour);
    const int w = (int) std::ceil (layout.getWidth()) + 22, h = (int) std::ceil (layout.getHeight()) + 14;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}
void TriadLook::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto m = menuColours();
    g.fillAll (m.back);                                   // the tooltip's window is square (opaque)
    g.setColour (m.edge);
    g.drawRect (0, 0, width, height, 1);
    tooltipLayout (text, m.text).draw (g, juce::Rectangle<float> (11.0f, 7.0f, (float) width - 22.0f, (float) height - 14.0f));
}
void TriadLook::drawComboBox (juce::Graphics& g, int width, int height, bool down, int, int, int, int, juce::ComboBox& box)
{
    const auto m = menuColours();
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (currentPalette.controls.brighter (down ? 0.08f : box.isMouseOver (true) ? 0.04f : 0.0f));
    g.fillRoundedRectangle (r, 7.0f);
    if (box.isPopupActive() || box.hasKeyboardFocus (true))
    {
        g.setColour (m.chosen);
        g.drawRoundedRectangle (r.reduced (0.75f), 7.0f, 1.5f);
    }
    juce::Path arrow;
    const float ax = (float) width - 20.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax, ay - 3.0f, ax + 9.0f, ay - 3.0f, ax + 4.5f, ay + 3.5f);
    g.setColour (m.text.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillPath (arrow);
}
juce::Font TriadLook::getComboBoxFont (juce::ComboBox&) { return juce::FontOptions (13.5f, juce::Font::bold); }
void TriadLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}
}
