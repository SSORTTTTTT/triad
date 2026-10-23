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
TriadLook::TriadLook()
{
   #if JUCE_WINDOWS
    // Text as on the Mac: JUCE's default on Windows (Verdana) is far wider than the Mac's Helvetica the layout was
    // made with; Arial has Helvetica's letter widths. Fonts are found through the default look (this plug-in's own
    // copy of JUCE), which only clears its font cache when the name changes.
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName ("Arial");
    setDefaultSansSerifTypefaceName ("Arial");
   #endif
    syncThemeFromSettings();
    applyTheme();
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
    setColour (juce::PopupMenu::backgroundColourId, p.panel);
    setColour (juce::PopupMenu::textColourId, p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.controlsOn);
    setColour (juce::PopupMenu::highlightedTextColourId, p.text);
    setColour (juce::PopupMenu::headerTextColourId, p.accent);
    setColour (juce::TooltipWindow::backgroundColourId, p.grid);
    setColour (juce::TooltipWindow::textColourId, p.text);
    setColour (juce::TooltipWindow::outlineColourId, p.fieldOutline);
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
void TriadLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& slider)
{
    const bool small = juce::jmin (w, h) < 40;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (small ? 1.5f : 5.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float stroke = small ? 2.5f : 4.5f;
    const auto centre = bounds.getCentre();
    // Dark body like FL's slot knobs, with the value arc in the band colour around it.
    g.setColour (currentPalette.knobBody);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    const auto arcRadius = radius - stroke * 0.5f;
    juce::Path track, value;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0, start, end, true);
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0, start, start + pos * (end - start), true);
    g.setColour (currentPalette.knobTrack); g.strokePath (track, juce::PathStrokeType (stroke));
    g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId)); g.strokePath (value, juce::PathStrokeType (stroke));
    // The percentage, inside the circle.
    const auto v = slider.getValue();
    // Small knobs show just the number, sized to fit inside the ring (the slot's tooltip says it is a %).
    // A knob in milliseconds says so (Triad Time's smoothing, attack and release); every other knob is a percentage.
    const bool ms = slider.getProperties().getWithDefault ("triadUnit", "%").toString() == "ms";
    const auto label = ms ? (v < 10.0 ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + (small ? "" : " ms")
                          : (v >= 99.95 ? juce::String ("100") : juce::String (juce::roundToInt (v))) + (small ? "" : "%");
    g.setColour (textColour.withAlpha (slider.isEnabled() ? 0.95f : 0.5f));
    const float innerWidth = (arcRadius - stroke * 0.5f) * 2.0f;
    g.setFont (small ? juce::FontOptions (juce::jlimit (7.5f, 9.5f, innerWidth * 0.45f), juce::Font::bold)
                     : juce::FontOptions (juce::jmax (11.0f, radius * 0.46f), juce::Font::bold));
    const auto inner = juce::Rectangle<float> (innerWidth, radius).withCentre (centre).toNearestInt();
    g.drawFittedText (label, inner, juce::Justification::centred, 1, 0.85f);
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
void TriadLook::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& text,
                                                       const juce::PopupMenu::Options&)
{
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (findColour (juce::PopupMenu::headerTextColourId));
    g.drawFittedText (text, area.getX() + 12, area.getY(), area.getWidth() - 16, (int) ((float) area.getHeight() * 0.8f),
                      juce::Justification::bottomLeft, 1);
}
}
