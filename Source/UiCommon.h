#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace triad::ui
{
// One colour per band position (up to 10 bands).
inline juce::Colour bandColour (int band)
{
    static const juce::Colour colours[] { juce::Colour (0xff9bd85c), juce::Colour (0xff67c6b8), juce::Colour (0xffa39ce8),
                                          juce::Colour (0xffe8b15c), juce::Colour (0xffe87f8a), juce::Colour (0xff5ca8e8),
                                          juce::Colour (0xffd88ce0), juce::Colour (0xffc9d65c), juce::Colour (0xff5cdca2),
                                          juce::Colour (0xffe8955c) };
    if (band == 10) return juce::Colour (0xffd9dcd6);   // the PRE chain (TriadProcessor::preBand)
    if (band >= 11) return juce::Colour (0xffc3cde0);   // the POST chain (TriadProcessor::postBand)
    return colours[juce::jlimit (0, 9, band)];
}
// Every colour of Triad's interface except the band colours (which never change): set by the theme (Theme.h), the
// same in every window of this plug-in. Read through the names below (background, panel, textColour ...), so whatever
// is drawn next uses the theme of the moment.
struct Palette
{
    juce::Colour background, panel, header, controls, controlsHover, controlsOn, iconFill, iconOver, grid, gridLines,
                 text, dimText, softText, faintText, accent, led, ledOff, ledOffOver, knobBody, knobTrack, sliderTrack,
                 sliderThumb, sliderThumbLine, field, fieldOutline, scrollThumb, marker, spectrum, meterBack, separator,
                 paletteButton, tabOn, tabOver, timeLine, volumeLine, pill;
};
// Triad's own dark look (the default theme), colour for colour as it has always been drawn.
inline Palette legacyPalette()
{
    Palette p;
    p.background = juce::Colour (0xff252729);   p.panel = juce::Colour (0xff1b1e1c);        p.header = juce::Colour (0xff303235);
    p.controls = juce::Colour (0xff2e3230);     p.controlsHover = juce::Colour (0xff363b38); p.controlsOn = juce::Colour (0xff3d4a36);
    p.iconFill = juce::Colour (0xff2a2e2c);     p.iconOver = juce::Colour (0xff353a37);
    p.grid = juce::Colour (0xff111312);         p.gridLines = juce::Colour (0xffcfd5cc);
    p.text = juce::Colour (0xffe7eae5);         p.dimText = juce::Colour (0xffa6afa5);      p.softText = juce::Colour (0xffb8bcba);
    p.faintText = juce::Colour (0xff8f9690);    p.accent = juce::Colour (0xff9bd85c);       p.led = juce::Colour (0xff8fe04a);
    p.ledOff = juce::Colour (0xff4a504c);       p.ledOffOver = juce::Colour (0xff5d645f);
    p.knobBody = juce::Colour (0xff131615);     p.knobTrack = juce::Colour (0xff3b413a);
    p.sliderTrack = juce::Colour (0xff141716);  p.sliderThumb = juce::Colour (0xffd7ddd4);  p.sliderThumbLine = juce::Colour (0xff7a827c);
    p.field = juce::Colour (0xff0d0f0e);        p.fieldOutline = juce::Colour (0xff3d4240); p.scrollThumb = juce::Colour (0xff4a504c);
    p.marker = juce::Colour (0xfff2f5ef);       p.spectrum = juce::Colour (0xffe8ece6);     p.meterBack = juce::Colour (0xff1c1f1e);
    p.separator = juce::Colour (0xff3a3f3c);    p.paletteButton = juce::Colour (0xff1a1d1c);
    p.tabOn = juce::Colour (0xff242826);        p.tabOver = juce::Colour (0xff383c3a);
    p.timeLine = juce::Colour (0xff6fb7e9);     p.volumeLine = juce::Colour (0xffe9a96f);   p.pill = juce::Colour (0xff090b09);
    return p;
}
inline Palette currentPalette = legacyPalette();
inline int themeGeneration = 0;   // goes up with every theme change (cached pictures redraw)
inline const juce::Colour& background = currentPalette.background, & panel = currentPalette.panel,
                        & textColour = currentPalette.text, & dimTextColour = currentPalette.dimText,
                        & slotRow = currentPalette.controls, & slotRowHover = currentPalette.controlsHover,
                        & ledColour = currentPalette.led, & accent = currentPalette.accent,
                        & headerColour = currentPalette.header, & gridBackground = currentPalette.grid;
// Dark text on a bright fill (a lit band button, a selected tab): stays dark in every theme.
const juce::Colour inkColour (0xff111312);
// Text with non-ASCII characters (middle dot, arrows) written as UTF-8 in the source.
inline juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
// The favourites heart (0.15.0, Thomas: "a small outline of a heart, and then when you click it, it fills up to be
// red"): an outline, brighter under the mouse, filled red when it is a favourite.
const juce::Colour heartColour (0xffe5484d);
// SOLO (0.18.0): yellow when on, as in most DAWs (MUTE is the warm red 0xffe8705c).
const juce::Colour soloColour (0xffe8c95c);
inline juce::Path heartPath (juce::Rectangle<float> r)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    const auto b = r.withSizeKeepingCentre (s, s * 0.9f);
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    juce::Path p;
    p.startNewSubPath (x + w * 0.5f, y + h);                                                      // the point at the bottom
    p.cubicTo (x + w * 0.38f, y + h * 0.84f, x, y + h * 0.62f, x, y + h * 0.32f);                  // up the left side
    p.cubicTo (x, y + h * 0.08f, x + w * 0.18f, y, x + w * 0.29f, y);                              // over the left lobe
    p.cubicTo (x + w * 0.4f, y, x + w * 0.48f, y + h * 0.07f, x + w * 0.5f, y + h * 0.2f);
    p.cubicTo (x + w * 0.52f, y + h * 0.07f, x + w * 0.6f, y, x + w * 0.71f, y);                   // the right lobe
    p.cubicTo (x + w * 0.82f, y, x + w, y + h * 0.08f, x + w, y + h * 0.32f);
    p.cubicTo (x + w, y + h * 0.62f, x + w * 0.62f, y + h * 0.84f, x + w * 0.5f, y + h);         // down to the point
    p.closeSubPath();
    return p;
}
inline void drawHeart (juce::Graphics& g, juce::Rectangle<float> area, bool favourite, bool hover, juce::Colour outline)
{
    const auto p = heartPath (area);
    if (favourite)
    {
        g.setColour (heartColour);
        g.fillPath (p);
        return;
    }
    if (hover) { g.setColour (heartColour.withAlpha (0.25f)); g.fillPath (p); }
    g.setColour (hover ? heartColour : outline);
    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.1f, area.getHeight() * 0.09f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
// Decibels for meters and readouts: "-6.0 dB", "-inf".
inline juce::String formatDb (float gain)
{
    if (gain < 1.0e-4f) return "-inf";
    const auto db = juce::Decibels::gainToDecibels (gain);
    return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
}

// Latency in milliseconds: "4.1 ms" below 10 ms, whole milliseconds above ("57 ms", not "57.4375 ms").
inline juce::String formatMs (double ms)
{
    return ms < 10.0 ? juce::String (ms, 1) + " ms" : juce::String (juce::roundToInt (ms)) + " ms";
}
// Readable frequency labels: "37.6 Hz", "133 Hz", "2.5 kHz", "10.95 kHz".
inline juce::String formatHz (float hz)
{
    auto trimmed = [] (const juce::String& s) { return s.containsChar ('.') ? s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".") : s; };
    if (hz >= 1000.0f) return trimmed (juce::String (hz / 1000.0f, 2)) + " kHz";
    if (hz >= 100.0f) return juce::String (juce::roundToInt (hz)) + " Hz";
    return trimmed (juce::String (hz, 1)) + " Hz";
}
// Accepts "250", "250 Hz", "2.5k", "2.5 kHz". Returns <= 0 if the text is not a frequency.
inline float parseHz (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    if (t.isEmpty() || ! juce::CharacterFunctions::isDigit (t[0])) return -1.0f;
    const auto value = t.getFloatValue();
    return t.containsChar ('k') ? value * 1000.0f : value;
}

// A picture cached at the screen's pixel scale, put back pixel for pixel. (Smoothing it on the way, the default,
// blurs it whenever it lands half a pixel off: at window sizes such as 125%, or a window at an odd position.)
inline void drawPixelExact (juce::Graphics& g, const juce::Image& image, float scale)
{
    juce::Graphics::ScopedSaveState state (g);
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale));
}

// The frame rate of everything that moves (meters, spectrum, playheads, LFO dots, dividers under automation).
// Every animation asks it on each frame of the screen whether to move on. It runs at the screen's rate up to
// 120 frames a second (ProMotion) while the computer keeps up, and steps down to 60 when frames come late, the
// drawing takes too long or the audio is busy; it tries 120 again a few seconds later (waiting longer each time
// it had to step down). One per plug-in binary, shared by all its windows.
class FramePacer
{
public:
    static constexpr double fastInterval = 1.0 / 130.0, normalInterval = 1.0 / 65.0;
    // `now`: the screen's frame time (seconds); `last`: the caller's last frame. True: draw this frame.
    bool due (double& last, double now)
    {
        observe (now);
        if (last > 0.0 && now - last < (fast ? fastInterval : normalInterval)) return false;
        last = now;
        return true;
    }
    // Time spent drawing or working out what moves (each frame's total is what counts).
    void addWork (double ms) noexcept { work += ms; }
    struct Timed
    {
        explicit Timed (FramePacer& p) : pacer (p) {}
        ~Timed() { pacer.addWork (juce::Time::getMillisecondCounterHiRes() - start); }
        FramePacer& pacer;
        double start = juce::Time::getMillisecondCounterHiRes();
    };
    void setAudioLoad (float load) noexcept { audioLoad = load; }   // 0..1 of the audio buffer's time (Triad's CPU meter)
    bool isFast() const noexcept { return fast; }
    double getWorkMs() const noexcept { return workAverage; }
private:
    void observe (double now)
    {
        if (juce::exactlyEqual (now, lastSeen)) return;   // every animation gets the same frame: count it once
        if (lastSeen > 0.0 && now > lastSeen)
        {
            const double gap = juce::jmin (0.25, now - lastSeen);
            interval += 0.1 * (gap - interval);
            // The screen's own rate: the shortest gap between frames lately (no screen refreshes faster than 240 Hz).
            if (gap >= 1.0 / 240.0) screenPeriod = juce::jmin (gap, screenPeriod + 0.002 * (gap - screenPeriod));
            workAverage += 0.1 * (work - workAverage);
            const bool strained = interval > screenPeriod * 1.35 || workAverage > (fast ? 5.0 : 9.0) || audioLoad > 0.75f;
            if (fast)
            {
                strainedFor = strained ? strainedFor + gap : 0.0;
                if (strainedFor > 0.5)
                {
                    fast = false;
                    retryAt = now + backoff;
                    backoff = juce::jmin (60.0, backoff * 2.0);
                }
            }
            else if (now >= retryAt && screenPeriod < 1.0 / 90.0 && interval < screenPeriod * 1.15 && workAverage < 2.5 && audioLoad < 0.5f)
            {
                fast = true;
                strainedFor = 0.0;
            }
            if (fast && now - lastFastSince > 30.0) backoff = 3.0;   // ran well a while: step down sooner next time
            if (! fast) lastFastSince = now;
        }
        lastSeen = now;
        work = 0.0;
    }
    bool fast = true;
    double lastSeen = 0.0, interval = 1.0 / 60.0, screenPeriod = 1.0 / 60.0, work = 0.0, workAverage = 0.0;
    double strainedFor = 0.0, retryAt = 0.0, backoff = 3.0, lastFastSince = 0.0;
    float audioLoad = 0.0f;
};
}
