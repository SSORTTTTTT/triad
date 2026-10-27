#include "Theme.h"
#include "Look.h"

namespace triad::ui
{
namespace
{
// This plug-in's theme state (each plug-in binary has its own; they agree through Triad.settings).
struct ThemeState
{
    Theme current;
    juce::String lastSeen;   // the theme setting (name and colours) last read from, or written to, Triad.settings
    bool loaded = false;
    std::vector<ThemeWatcher*> watchers;
};
ThemeState& state()
{
    static ThemeState s { defaultTheme(), {}, false, {} };
    return s;
}
Theme make (const char* name, std::initializer_list<juce::uint32> rgb)
{
    Theme t;
    t.name = name;
    int i = 0;
    for (auto c : rgb) t.colours[(size_t) i++] = juce::Colour (0xff000000u | c);
    jassert (i == Theme::numRoles);
    return t;
}
float luminance (juce::Colour c)
{
    auto channel = [] (juce::uint8 v)
    {
        const float x = (float) v / 255.0f;
        return x <= 0.03928f ? x / 12.92f : std::pow ((x + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * channel (c.getRed()) + 0.7152f * channel (c.getGreen()) + 0.0722f * channel (c.getBlue());
}
juce::File themeFile (const juce::String& name) { return TriadSettings::themeFolder().getChildFile (juce::File::createLegalFileName (name) + ".triadtheme"); }
// What Triad.settings says the theme is (a saved theme's file date too, so re-saving it under the same name shows).
juce::String settingsKey (const TriadSettings& settings)
{
    const auto name = settings.getThemeName();
    if (name == "Custom") return name + "\n" + settings.getCustomTheme();
    if (name.startsWith ("My: ")) return name + "\n" + juce::String (themeFile (name.substring (4)).getLastModificationTime().toMilliseconds());
    return name;
}
}

// ---- Themes ---------------------------------------------------------------------------------------------------------
bool Theme::sameColours (const Theme& other) const
{
    for (int r = 0; r < numRoles; ++r)
        if ((*this)[r].getARGB() != other[r].getARGB()) return false;
    return true;
}
juce::String Theme::roleName (int role)
{
    static const char* names[] { "Background", "Panels", "Buttons and slots", "Grids and displays", "Grid lines", "Text",
                                 "Accent (titles, lit buttons)", "Triad Time: TIME line", "Triad Time: VOLUME line" };
    return names[juce::jlimit (0, numRoles - 1, role)];
}
juce::String Theme::roleKey (int role)
{
    static const char* keys[] { "background", "panels", "buttons", "grids", "gridLines", "text", "accent", "timeLine", "volumeLine" };
    return keys[juce::jlimit (0, numRoles - 1, role)];
}
juce::String Theme::toText() const
{
    juce::StringArray parts;
    for (int r = 0; r < numRoles; ++r) parts.add (roleKey (r) + "=#" + (*this)[r].toDisplayString (false));
    return parts.joinIntoString ("; ");
}
Theme Theme::fromText (const juce::String& text, const juce::String& name)
{
    auto t = defaultTheme();
    t.name = name;
    juce::StringArray parts;
    parts.addTokens (text, ";\n", "");
    for (const auto& part : parts)
    {
        const auto key = part.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = part.fromFirstOccurrenceOf ("=", false, false).trim().trimCharactersAtStart ("#");
        if (value.length() != 6 || ! value.containsOnly ("0123456789abcdefABCDEF")) continue;
        for (int r = 0; r < numRoles; ++r)
            if (key == roleKey (r)) t[r] = juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) value.getHexValue32()));
    }
    return t;
}
const std::vector<Theme>& builtInThemes()
{
    //                                   background panels    buttons   grids     grid lines text      accent    TIME      VOLUME
    static const std::vector<Theme> themes {
        make ("Triad",    { 0x252729, 0x1b1e1c, 0x2e3230, 0x111312, 0xcfd5cc, 0xe7eae5, 0x9bd85c, 0x6fb7e9, 0xe9a96f }),
        // The others keep their accents and lines clear of the ten band colours (none closer than 22).
        make ("Midnight", { 0x1b2130, 0x141925, 0x262e40, 0x0c1019, 0xc5d0ea, 0xe8edf7, 0x40e0ff, 0x8f6bff, 0xffd84d }),
        make ("Graphite", { 0x2b2b2b, 0x202020, 0x383838, 0x151515, 0xd6d6d6, 0xeeeeee, 0xff5c5c, 0x00e5ff, 0xffd84d }),
        make ("Black",    { 0x0b0b0b, 0x050505, 0x1c1c1c, 0x000000, 0xffffff, 0xf4f4f4, 0xf5f5f5, 0x00e5ff, 0xff4f6a }),
        make ("Plum",     { 0x2a1f30, 0x1e1624, 0x382a40, 0x140d18, 0xe2d2ea, 0xf2eaf6, 0xff6bd0, 0x8f6bff, 0xffb0cf }),
        make ("Forest",   { 0x1f2a23, 0x16201a, 0x2b3a30, 0x0d140f, 0xcfe0d3, 0xe6f0e8, 0xffd84d, 0x40e0ff, 0xff5c5c }) };
    return themes;
}
const Theme& defaultTheme() { return builtInThemes().front(); }

Palette paletteFor (const Theme& t)
{
    // Triad's own theme is drawn exactly as before; any other is worked out from its nine colours. Shades are mixed
    // towards the text colour, so they go lighter on a dark theme and darker on a light one.
    if (t.sameColours (defaultTheme())) return legacyPalette();
    const auto B = t[Theme::background], P = t[Theme::panels], C = t[Theme::buttons], G = t[Theme::grids], L = t[Theme::gridLines],
               T = t[Theme::text], A = t[Theme::accent];
    const auto mix = [] (juce::Colour a, juce::Colour b, float f) { return a.interpolatedWith (b, f); };
    Palette p;
    p.background = B;                   p.panel = P;                          p.header = mix (B, T, 0.055f);
    p.controls = C;                     p.controlsHover = mix (C, T, 0.05f);  p.controlsOn = mix (C, A, 0.2f);
    p.iconFill = mix (C, P, 0.25f);     p.iconOver = mix (C, T, 0.05f);
    p.grid = G;                         p.gridLines = L;
    p.text = T;                         p.dimText = mix (T, B, 0.32f);        p.softText = mix (T, B, 0.2f);
    p.faintText = mix (T, B, 0.42f);    p.accent = A;                         p.led = A;
    p.ledOff = mix (C, T, 0.13f);       p.ledOffOver = mix (C, T, 0.22f);
    p.knobBody = mix (G, C, 0.05f);     p.knobTrack = mix (C, T, 0.07f);
    p.sliderTrack = mix (G, C, 0.05f);  p.sliderThumb = mix (T, B, 0.06f);    p.sliderThumbLine = mix (T, B, 0.45f);
    p.field = G;                        p.fieldOutline = mix (C, T, 0.08f);   p.scrollThumb = mix (C, T, 0.13f);
    p.marker = luminance (G) < 0.35f ? juce::Colour (0xfff2f5ef) : juce::Colour (0xff1d201e);
    p.spectrum = T;                     p.meterBack = mix (G, C, 0.4f);       p.separator = mix (C, T, 0.05f);
    p.paletteButton = mix (P, B, 0.15f); p.tabOn = mix (B, G, 0.08f);         p.tabOver = mix (B, T, 0.09f);
    p.timeLine = t[Theme::timeLine];    p.volumeLine = t[Theme::volumeLine];  p.pill = G;
    return p;
}

namespace
{
std::array<float, 3> lab (juce::Colour c)
{
    auto channel = [] (juce::uint8 v)
    {
        const float x = (float) v / 255.0f;
        return x <= 0.04045f ? x / 12.92f : std::pow ((x + 0.055f) / 1.055f, 2.4f);
    };
    const float r = channel (c.getRed()), g = channel (c.getGreen()), b = channel (c.getBlue());
    const float x = (0.4124f * r + 0.3576f * g + 0.1805f * b) / 0.95047f, y = 0.2126f * r + 0.7152f * g + 0.0722f * b,
                z = (0.0193f * r + 0.1192f * g + 0.9505f * b) / 1.08883f;
    auto f = [] (float t) { return t > 0.008856f ? std::cbrt (t) : 7.787f * t + 16.0f / 116.0f; };
    return { 116.0f * f (y) - 16.0f, 500.0f * (f (x) - f (y)), 200.0f * (f (y) - f (z)) };
}
bool isBackgroundRole (int role) { return role == Theme::background || role == Theme::panels || role == Theme::buttons || role == Theme::grids; }
float closestBand (juce::Colour c)
{
    float d = 0.0f;
    nearestBand (c, &d);
    return d;
}
}
float colourDistance (juce::Colour a, juce::Colour b)
{
    const auto p = lab (a), q = lab (b);
    return std::sqrt ((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}
int nearestBand (juce::Colour c, float* distance)
{
    int best = 0;
    float bestDistance = 1.0e9f;
    for (int b = 0; b < 10; ++b)
        if (const auto d = colourDistance (c, bandColour (b)); d < bestDistance) { bestDistance = d; best = b; }
    if (distance != nullptr) *distance = bestDistance;
    return best;
}
juce::Colour keepClearOfBands (int role, juce::Colour c)
{
    c = c.withAlpha (1.0f);
    if (isBackgroundRole (role))
    {
        // Behind the bands: dark enough that every band colour stands out on it (the band colours are light).
        const auto weakest = [] (juce::Colour back)
        {
            float worst = 100.0f;
            for (int b = 0; b < 10; ++b) worst = juce::jmin (worst, contrast (bandColour (b), back));
            return worst;
        };
        for (int i = 0; i < 40 && (weakest (c) < 2.2f || closestBand (c) < bandClearance); ++i) c = c.interpolatedWith (juce::Colours::black, 0.1f);
        return c;
    }
    if (closestBand (c) >= bandClearance) return c;
    // The nearest colour (to the one picked) that is clear of every band: turn the hue a little, make it lighter or
    // darker, or less saturated.
    const float hue = c.getHue(), saturation = c.getSaturation(), brightness = c.getBrightness();
    juce::Colour best = c;
    float bestCost = 1.0e9f;
    for (int dh = -60; dh <= 60; dh += 5)
        for (float vf : { 1.0f, 0.85f, 1.15f, 0.7f, 1.3f, 0.55f })
            for (float sf : { 1.0f, 0.75f, 0.5f, 0.25f })
            {
                float h = hue + (float) dh / 360.0f;
                h -= std::floor (h);
                const auto candidate = juce::Colour::fromHSV (h, juce::jlimit (0.0f, 1.0f, saturation * sf), juce::jlimit (0.0f, 1.0f, brightness * vf), 1.0f);
                if (closestBand (candidate) < bandClearance) continue;
                if (const auto cost = colourDistance (c, candidate); cost < bestCost) { bestCost = cost; best = candidate; }
            }
    return best;
}
Theme keepClearOfBands (Theme t)
{
    if (t.sameColours (defaultTheme())) return t;   // Triad's own theme, as it has always been
    for (int r = 0; r < Theme::numRoles; ++r) t[r] = keepClearOfBands (r, t[r]);
    return t;
}

float contrast (juce::Colour a, juce::Colour b)
{
    const auto la = luminance (a), lb = luminance (b);
    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}
juce::StringArray readabilityProblems (const Theme& t)
{
    juce::StringArray problems;
    const auto text = t[Theme::text];
    if (contrast (text, t[Theme::background]) < 4.0f || contrast (text, t[Theme::panels]) < 4.0f) problems.add ("Text is hard to read on the background or panels");
    else if (contrast (text, t[Theme::buttons]) < 3.0f) problems.add ("Text is hard to read on the buttons");
    if (contrast (t[Theme::gridLines], t[Theme::grids]) < 1.6f) problems.add ("Grid lines are hard to see");
    if (contrast (t[Theme::timeLine], t[Theme::grids]) < 2.2f) problems.add ("The TIME line is hard to see on the grid");
    if (contrast (t[Theme::volumeLine], t[Theme::grids]) < 2.2f) problems.add ("The VOLUME line is hard to see on the grid");
    if (contrast (t[Theme::accent], t[Theme::panels]) < 2.0f) problems.add ("The accent is hard to see on the panels");
    return problems;
}
Theme makeReadable (Theme t)
{
    const juce::Colour light (0xffe7eae5), dark (0xff1f2320);
    const auto worst = [&t] (juce::Colour text)
    {
        return std::min ({ contrast (text, t[Theme::background]), contrast (text, t[Theme::panels]), contrast (text, t[Theme::buttons]) });
    };
    if (worst (t[Theme::text]) < 4.0f) t[Theme::text] = worst (light) >= worst (dark) ? light : dark;
    // Lines and the accent: lighter on a dark ground, darker on a light one, until they stand out.
    const auto push = [&t] (int role, int against, float needed)
    {
        const auto towards = luminance (t[against]) < 0.35f ? juce::Colours::white : juce::Colours::black;
        for (int i = 0; i < 24 && contrast (t[role], t[against]) < needed; ++i) t[role] = t[role].interpolatedWith (towards, 0.15f);
    };
    push (Theme::gridLines, Theme::grids, 1.6f);
    push (Theme::timeLine, Theme::grids, 2.2f);
    push (Theme::volumeLine, Theme::grids, 2.2f);
    push (Theme::accent, Theme::panels, 2.0f);
    return keepClearOfBands (t);
}

Theme randomTheme (juce::Random& r)
{
    const auto hsv = [] (float h, float s, float v)
    {
        h -= std::floor (h);
        return juce::Colour::fromHSV (h, juce::jlimit (0.0f, 1.0f, s), juce::jlimit (0.0f, 1.0f, v), 1.0f);
    };
    // Colour schemes for the accent and the two lines, as turns of the hue wheel from the backgrounds' hue:
    // complementary, split complementary, triadic, tetradic, analogous and opposite.
    static const float schemes[][3] { { 0.5f, 0.33f, 0.67f }, { 0.0f, 0.42f, 0.58f }, { 0.08f, 0.5f, 0.92f },
                                      { 0.33f, 0.67f, 0.0f }, { 0.25f, 0.5f, 0.75f }, { 0.5f, 0.08f, 0.92f } };
    for (int attempt = 0; attempt < 400; ++attempt)
    {
        Theme t;
        t.name = "Custom";
        const float hue = r.nextFloat(), tint = 0.1f + 0.3f * r.nextFloat(), depth = 0.1f + 0.08f * r.nextFloat();
        t[Theme::background] = hsv (hue, tint, depth);
        t[Theme::panels] = hsv (hue, tint * 1.05f, depth * 0.74f);
        t[Theme::buttons] = hsv (hue, tint * 0.9f, depth * 1.3f + 0.02f);
        t[Theme::grids] = hsv (hue, tint * 1.1f, depth * 0.45f);
        t[Theme::gridLines] = hsv (hue, 0.05f + 0.1f * r.nextFloat(), 0.8f + 0.12f * r.nextFloat());
        t[Theme::text] = hsv (hue, 0.02f + 0.05f * r.nextFloat(), 0.92f + 0.07f * r.nextFloat());
        const auto& scheme = schemes[r.nextInt ((int) std::size (schemes))];
        const auto bright = [&] (float turn) { return hsv (hue + turn + (r.nextFloat() - 0.5f) * 0.06f, 0.5f + 0.45f * r.nextFloat(), 0.82f + 0.18f * r.nextFloat()); };
        t[Theme::accent] = bright (scheme[0]);
        t[Theme::timeLine] = bright (scheme[1]);
        t[Theme::volumeLine] = bright (scheme[2]);
        t = makeReadable (keepClearOfBands (t));
        t.name = "Custom";
        // Every colour clear of the bands and readable; the accent and the lines tell apart; not a theme there is already.
        if (! readabilityProblems (t).isEmpty() || ! keepClearOfBands (t).sameColours (t)) continue;
        if (colourDistance (t[Theme::accent], t[Theme::timeLine]) < bandClearance || colourDistance (t[Theme::accent], t[Theme::volumeLine]) < bandClearance
            || colourDistance (t[Theme::timeLine], t[Theme::volumeLine]) < 25.0f)
            continue;
        bool copy = false;
        for (const auto& other : builtInThemes())
            copy |= colourDistance (other[Theme::background], t[Theme::background]) < 6.0f && colourDistance (other[Theme::accent], t[Theme::accent]) < 15.0f;
        if (! copy) return t;
    }
    auto fallback = builtInThemes()[1];
    fallback.name = "Custom";
    return fallback;
}

// ---- The current theme --------------------------------------------------------------------------------------------
const Theme& currentTheme() { return state().current; }
void applyTheme (const Theme& theme, bool save)
{
    auto& s = state();
    s.current = theme;
    currentPalette = paletteFor (theme);
    ++themeGeneration;
    if (save)
    {
        juce::SharedResourcePointer<TriadSettings> settings;
        settings->setTheme (theme.name, theme.name == "Custom" ? theme.toText() : juce::String());
        s.lastSeen = settingsKey (*settings);
        s.loaded = true;
    }
    const auto watchers = s.watchers;   // a window may close while another redraws
    for (auto* w : watchers)
        if (std::find (s.watchers.begin(), s.watchers.end(), w) != s.watchers.end()) w->notify();
}
Theme themeNamed (const juce::String& name)
{
    for (const auto& t : builtInThemes())
        if (t.name == name) return t;
    if (name == "Custom")
    {
        juce::SharedResourcePointer<TriadSettings> settings;
        if (const auto text = settings->getCustomTheme(); text.isNotEmpty()) return keepClearOfBands (Theme::fromText (text, "Custom"));
    }
    if (name.startsWith ("My: "))
        if (const auto file = themeFile (name.substring (4)); file.existsAsFile())
            return keepClearOfBands (Theme::fromText (file.loadFileAsString(), name));   // a file edited by hand too
    return defaultTheme();
}
juce::StringArray savedThemeNames()
{
    juce::StringArray names;
    for (const auto& f : TriadSettings::themeFolder().findChildFiles (juce::File::findFiles, false, "*.triadtheme"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}
bool saveTheme (const juce::String& name, const Theme& theme)
{
    if (juce::File::createLegalFileName (name).isEmpty() || ! TriadSettings::themeFolder().createDirectory()) return false;
    return themeFile (name).replaceWithText (theme.toText() + "\n");
}
void syncThemeFromSettings()
{
    juce::SharedResourcePointer<TriadSettings> settings;
    auto& s = state();
    const auto key = settingsKey (*settings);
    if (s.loaded && key == s.lastSeen) return;
    s.loaded = true;
    s.lastSeen = key;
    const auto theme = themeNamed (settings->getThemeName());
    if (! theme.sameColours (s.current) || theme.name != s.current.name) applyTheme (theme, false);
}

ThemeWatcher::ThemeWatcher (std::function<void()> changed) : onChange (std::move (changed))
{
    syncThemeFromSettings();   // before joining: the window colours itself once it is built
    state().watchers.push_back (this);
    startTimer (500);
}
ThemeWatcher::~ThemeWatcher()
{
    auto& w = state().watchers;
    w.erase (std::remove (w.begin(), w.end(), this), w.end());
}

// ---- Menu ---------------------------------------------------------------------------------------------------------------
juce::Array<juce::Colour> themeSwatches (const Theme& t)
{
    return { t[Theme::background], t[Theme::buttons], t[Theme::accent], t[Theme::timeLine] };
}
juce::String themeDisplayName (const Theme& t)
{
    return t.name.startsWith ("My: ") ? t.name.substring (4) : t.name;
}
void addThemeItems (juce::PopupMenu& menu, int firstId, bool full)
{
    if (full) menu.addSectionHeader ("THEME");
    const auto& current = currentTheme();
    int i = 0;
    for (const auto& t : builtInThemes())
    {
        addItemWithRight (menu, firstId + i, t.name, menuSwatches (themeSwatches (t)), true, current.name == t.name);
        ++i;
    }
    const auto saved = savedThemeNames();
    if (! saved.isEmpty())
    {
        juce::PopupMenu mine;
        for (int j = 0; j < saved.size() && j < 99; ++j)
            addItemWithRight (mine, firstId + 100 + j, saved[j], menuSwatches (themeSwatches (themeNamed ("My: " + saved[j]))), true,
                              current.name == "My: " + saved[j]);
        addSubMenu (menu, "My themes", std::move (mine), {}, true, current.name.startsWith ("My: "));
    }
    juce::SharedResourcePointer<TriadSettings> settings;
    if (settings->getCustomTheme().isNotEmpty() || current.name == "Custom")
        addItemWithRight (menu, firstId + 200, "Custom", menuSwatches (themeSwatches (themeNamed ("Custom"))), true, current.name == "Custom");
    if (full) menu.addItem (firstId + 201, "Edit colours...");
}
bool handleThemeChoice (int choice, int firstId, const std::function<void()>& openEditor)
{
    const auto& themes = builtInThemes();
    if (choice >= firstId && choice < firstId + (int) themes.size()) { applyTheme (themes[(size_t) (choice - firstId)], true); return true; }
    if (choice >= firstId + 100 && choice < firstId + 200)
    {
        const auto saved = savedThemeNames();
        if (const int j = choice - firstId - 100; j < saved.size()) applyTheme (themeNamed ("My: " + saved[j]), true);
        return true;
    }
    if (choice == firstId + 200) { applyTheme (themeNamed ("Custom"), true); return true; }
    if (choice == firstId + 201) { if (openEditor) openEditor(); return true; }
    return false;
}

// ---- Edit colours (0.16.0: the same layout, cleaner; Thomas) ---------------------------------------------------------------
// RANDOM COLOURWAY: a dice and the words.
struct ThemePanel::RandomButton final : public juce::Button
{
    RandomButton() : Button ("Random colourway") { setTooltip ("A random colour scheme: clear of the band colours, readable. Roll again, change any colour, or SAVE AS to keep it"); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (currentPalette.controls.brighter (down ? 0.12f : over ? 0.06f : 0.0f));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        const float size = r.getHeight() * 0.52f;
        auto content = r.withSizeKeepingCentre (size + 10.0f + 160.0f, r.getHeight());
        IconButton::drawIcon (g, IconButton::Icon::dice, content.removeFromLeft (size).withSizeKeepingCentre (size, size), textColour);
        content.removeFromLeft (10.0f);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText ("RANDOM COLOURWAY", content, juce::Justification::centredLeft);
    }
};
// START FROM: the theme's name and its colours as small squares; a click opens the themes.
struct ThemePanel::StartFrom final : public juce::Button
{
    StartFrom() : Button ("Start from")
    {
        setTooltip ("Start from a ready-made or saved theme");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto m = menuColours();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (currentPalette.controls.brighter (down ? 0.1f : over ? 0.05f : 0.0f));
        g.fillRoundedRectangle (r, 8.0f);
        if (open)
        {
            g.setColour (m.chosen);
            g.drawRoundedRectangle (r.reduced (0.75f), 8.0f, 1.5f);
        }
        auto inside = r.reduced (12.0f, 0.0f);
        juce::Path arrow;
        const float ax = inside.getRight() - 9.0f, ay = r.getCentreY();
        arrow.addTriangle (ax, ay - 3.0f, ax + 9.0f, ay - 3.0f, ax + 4.5f, ay + 3.5f);
        g.setColour (m.text);
        g.fillPath (arrow);
        inside.removeFromRight (22.0f);
        float sx = inside.getRight() - (float) swatches.size() * 14.0f + 3.0f;
        for (auto c : swatches)
        {
            const auto square = juce::Rectangle<float> (sx, ay - 5.5f, 11.0f, 11.0f);
            g.setColour (c);
            g.fillRoundedRectangle (square, 2.0f);
            g.setColour (m.text.withAlpha (0.35f));
            g.drawRoundedRectangle (square.reduced (0.5f), 2.0f, 1.0f);
            sx += 14.0f;
        }
        inside.setRight (inside.getRight() - (float) swatches.size() * 14.0f - 8.0f);
        g.setColour (m.text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawFittedText (name, inside.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }
    juce::String name;
    juce::Array<juce::Colour> swatches;
    bool open = false;
};
// The picker for the chosen colour: a square (saturation across, brightness down), the hue bar standing beside it
// with an arrow on each side (Thomas: "keep the color-changing bar on the side, not underneath it"), and the colour as
// RED / GREEN / BLUE boxes (drag one, or double-click to type).
struct ThemePanel::Picker final : public juce::Component, public juce::ChangeBroadcaster
{
    Picker()
    {
        const char* names[] { "RED", "GREEN", "BLUE" };
        const juce::uint32 tints[] { 0xffff5c6a, 0xff7ce07c, 0xff6aa8ff };
        for (int i = 0; i < 3; ++i)
        {
            auto& s = channels[(size_t) i];
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (0.0, 255.0, 1.0);
            s.setMouseDragSensitivity (200);
            s.getProperties().set ("triadLabel", names[i]);
            s.getProperties().set ("triadUnit", "raw");
            s.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (tints[i]));
            s.setTooltip (juce::String (names[i]).toLowerCase() + " (0-255): drag left or right");
            s.onValueChange = [this] { if (! updating) fromChannels(); };
            addAndMakeVisible (s);
        }
    }
    juce::Colour getColour() const { return colour; }
    void setColour (juce::Colour c)   // from the editor: no change message
    {
        c = c.withAlpha (1.0f);
        if (c == colour && ! first) return;
        first = false;
        colour = c;
        if (c.getSaturation() > 0.001f && c.getBrightness() > 0.001f) hue = c.getHue();
        saturation = c.getSaturation();
        brightness = c.getBrightness();
        showChannels();
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        preview = r.removeFromTop (30).toFloat();
        r.removeFromTop (10);
        auto boxes = r.removeFromBottom (44);
        r.removeFromBottom (10);
        hueBar = r.removeFromRight (34).withTrimmedLeft (7).withTrimmedRight (7).toFloat();
        r.removeFromRight (6);
        square = r.toFloat();
        const int w = (boxes.getWidth() - 16) / 3;
        for (auto& s : channels) { s.setBounds (boxes.removeFromLeft (w)); boxes.removeFromLeft (8); }
        squareImage = {};
    }
    void paint (juce::Graphics& g) override
    {
        // The colour, with its hex code on it.
        g.setColour (colour);
        g.fillRoundedRectangle (preview, 8.0f);
        g.setColour (colour.getPerceivedBrightness() > 0.55f ? inkColour : juce::Colours::white);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.5f, juce::Font::bold));
        g.drawText ("#" + colour.toDisplayString (false), preview, juce::Justification::centred);
        // The square for this hue (drawn again only when the hue or the size changes).
        if (squareImage.isNull() || ! juce::approximatelyEqual (squareHue, hue)) drawSquare();
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (square, 8.0f);
            g.reduceClipRegion (clip);
            g.drawImage (squareImage, square);
        }
        const auto spot = juce::Point<float> (square.getX() + saturation * square.getWidth(), square.getY() + (1.0f - brightness) * square.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (17.0f, 17.0f).withCentre (spot), 1.0f);
        g.setColour (juce::Colours::white);
        g.drawEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre (spot), 2.5f);
        // The hue bar: red at the top, round the colours to red again at the bottom.
        juce::ColourGradient rainbow (juce::Colours::red, 0.0f, hueBar.getY(), juce::Colours::red, 0.0f, hueBar.getBottom(), false);
        for (int i = 1; i < 6; ++i) rainbow.addColour ((double) i / 6.0, juce::Colour::fromHSV ((float) i / 6.0f, 1.0f, 1.0f, 1.0f));
        g.setGradientFill (rainbow);
        g.fillRoundedRectangle (hueBar, 6.0f);
        const float y = hueBar.getY() + hue * hueBar.getHeight();
        const auto handle = juce::Rectangle<float> (hueBar.getWidth() + 6.0f, 12.0f).withCentre ({ hueBar.getCentreX(), y });
        g.setColour (juce::Colour::fromHSV (hue, 1.0f, 1.0f, 1.0f));
        g.fillRoundedRectangle (handle, 4.0f);
        g.setColour (juce::Colours::white);
        g.drawRoundedRectangle (handle.reduced (1.25f), 4.0f, 2.5f);
        juce::Path arrows;
        arrows.addTriangle (hueBar.getX() - 10.0f, y - 6.0f, hueBar.getX() - 10.0f, y + 6.0f, hueBar.getX() - 4.0f, y);
        arrows.addTriangle (hueBar.getRight() + 10.0f, y - 6.0f, hueBar.getRight() + 10.0f, y + 6.0f, hueBar.getRight() + 4.0f, y);
        g.setColour (textColour);
        g.fillPath (arrows);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = square.expanded (4.0f).contains (e.position) ? 1 : hueBar.expanded (12.0f, 4.0f).contains (e.position) ? 2 : 0;
        mouseDrag (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == 1)
        {
            saturation = juce::jlimit (0.0f, 1.0f, (e.position.x - square.getX()) / square.getWidth());
            brightness = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - square.getY()) / square.getHeight());
        }
        else if (dragging == 2) hue = juce::jlimit (0.0f, 0.9999f, (e.position.y - hueBar.getY()) / hueBar.getHeight());
        else return;
        colour = juce::Colour::fromHSV (hue, saturation, brightness, 1.0f);
        showChannels();
        repaint();
        sendChangeMessage();
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = 0; }
    std::array<juce::Slider, 3> channels;
private:
    void showChannels()
    {
        const juce::ScopedValueSetter<bool> busy (updating, true);
        channels[0].setValue (colour.getRed(), juce::dontSendNotification);
        channels[1].setValue (colour.getGreen(), juce::dontSendNotification);
        channels[2].setValue (colour.getBlue(), juce::dontSendNotification);
    }
    void fromChannels()
    {
        colour = juce::Colour ((juce::uint8) juce::roundToInt (channels[0].getValue()), (juce::uint8) juce::roundToInt (channels[1].getValue()),
                               (juce::uint8) juce::roundToInt (channels[2].getValue()));
        if (colour.getSaturation() > 0.001f && colour.getBrightness() > 0.001f) hue = colour.getHue();
        saturation = colour.getSaturation();
        brightness = colour.getBrightness();
        repaint();
        sendChangeMessage();
    }
    void drawSquare()
    {
        squareHue = hue;
        const int w = juce::jmax (1, (int) square.getWidth()), h = juce::jmax (1, (int) square.getHeight());
        squareImage = juce::Image (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData pixels (squareImage, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                pixels.setPixelColour (x, y, juce::Colour::fromHSV (hue, (float) x / (float) juce::jmax (1, w - 1),
                                                                    1.0f - (float) y / (float) juce::jmax (1, h - 1), 1.0f));
    }
    juce::Colour colour;
    float hue = 0.0f, saturation = 0.0f, brightness = 0.0f, squareHue = -1.0f;
    juce::Rectangle<float> preview, square, hueBar;
    juce::Image squareImage;
    int dragging = 0;
    bool updating = false, first = true;
};
struct ThemePanel::Row final : public juce::Component
{
    Row (ThemePanel& p, int r) : owner (p), role (r)
    {
        hex.setInputRestrictions (7, "#0123456789abcdefABCDEF");
        hex.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold));
        hex.setIndents (8, 0);
        hex.setJustification (juce::Justification::centredLeft);
        hex.setTooltip ("The colour as a hex code (#RRGGBB): type one and press Return");
        hex.onReturnKey = [this] { typed(); };
        hex.onFocusLost = [this] { typed(); };
        addAndMakeVisible (hex);
        reset.setTooltip ("Back to Triad's own colour for this");
        reset.onClick = [this] { owner.setRoleColour (role, defaultTheme()[role]); };
        addAndMakeVisible (reset);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (Theme::roleName (role));
    }
    void typed()
    {
        auto text = hex.getText().trim().trimCharactersAtStart ("#");
        if (text.length() == 6) owner.setRoleColour (role, juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) text.getHexValue32())));
        else update (colour, chosen);
    }
    void update (juce::Colour c, bool isChosen)
    {
        colour = c;
        chosen = isChosen;
        if (! hex.hasKeyboardFocus (true)) hex.setText ("#" + c.toDisplayString (false), false);
        hex.setColour (juce::TextEditor::outlineColourId, isChosen ? menuColours().chosenInk.withAlpha (0.6f) : currentPalette.fieldOutline);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto m = menuColours();
        const auto r = getLocalBounds().toFloat();
        if (chosen)
        {
            g.setColour (m.chosen);
            g.fillRoundedRectangle (r.expanded (6.0f, 0.0f).withX (r.getX() - 6.0f), 8.0f);
        }
        // The colour as a square (0.16.0, Thomas: squares, not circles).
        const auto swatch = r.withWidth (r.getHeight()).reduced (5.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (swatch, 4.0f);
        g.setColour ((chosen ? m.chosenInk : textColour).withAlpha (0.45f));
        g.drawRoundedRectangle (swatch.reduced (0.75f), 4.0f, 1.5f);
        g.setColour (chosen ? m.chosenInk : textColour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (Theme::roleName (role), r.withTrimmedLeft (r.getHeight() + 6.0f).withTrimmedRight (128.0f), juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 4);
        reset.setBounds (r.removeFromRight (28));
        r.removeFromRight (8);
        hex.setBounds (r.removeFromRight (88));
    }
    void mouseDown (const juce::MouseEvent&) override { owner.selectRole (role); }
    ThemePanel& owner;
    int role;
    juce::Colour colour;
    bool chosen = false;
    juce::TextEditor hex;
    IconButton reset { IconButton::Icon::undo, "Back to Triad's own colour for this" };
};
ThemePanel::ThemePanel()
{
    setVisible (false);
    setWantsKeyboardFocus (true);
    for (int r = 0; r < Theme::numRoles; ++r)
    {
        rows.push_back (std::make_unique<Row> (*this, r));
        addAndMakeVisible (*rows.back());
    }
    picker = std::make_unique<Picker>();
    picker->addChangeListener (this);
    addAndMakeVisible (*picker);
    startFrom = std::make_unique<StartFrom>();
    startFrom->onClick = [this] { showStartFromMenu(); };
    addAndMakeVisible (*startFrom);
    saveName.setTextToShowWhenEmpty ("Name", dimTextColour);
    saveName.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    saveName.setIndents (10, 0);
    saveName.setJustification (juce::Justification::centredLeft);
    saveName.onReturnKey = [this] { saveButton.triggerClick(); };
    addAndMakeVisible (saveName);
    saveButton.setTooltip ("Keep these colours as a theme of your own (in Music/Triad/Themes; every Triad lists it)");
    saveButton.onClick = [this]
    {
        const auto name = juce::File::createLegalFileName (saveName.getText().trim());
        if (name.isEmpty()) { saveName.grabKeyboardFocus(); return; }
        if (! saveTheme (name, edited)) { problems.setText ("Could not save the theme in " + TriadSettings::themeFolder().getFullPathName(), juce::dontSendNotification); return; }
        edited.name = "My: " + name;
        applyTheme (edited, true);
        pendingSave = false;
        refresh();
        problems.setText ("Saved as \"" + name + "\" (gear menu > Theme > My themes)", juce::dontSendNotification);
    };
    addAndMakeVisible (saveButton);
    fixButton.setTooltip ("Pick text, line and accent colours that can be read on these backgrounds");
    fixButton.onClick = [this] { auto t = makeReadable (edited); t.name = "Custom"; note = {}; edit (t, false); };
    addAndMakeVisible (fixButton);
    doneButton.setTooltip ("Keep this theme: every Triad, Triad Time and Triad Split uses it from now on");
    doneButton.onClick = [this] { close(); };
    addAndMakeVisible (doneButton);
    cancelButton.setTooltip ("Back to the theme you had when you opened this");
    cancelButton.onClick = [this] { cancel(); };
    addAndMakeVisible (cancelButton);
    randomButton = std::make_unique<RandomButton>();
    randomButton->onClick = [this] { randomise(); };
    addAndMakeVisible (*randomButton);
    problems.setJustificationType (juce::Justification::centredLeft);
    problems.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (problems);
}
void ThemePanel::startFromTheme (int id)
{
    const auto& themes = builtInThemes();
    const auto saved = savedThemeNames();
    Theme chosen;
    if (id >= 1 && id <= (int) themes.size()) chosen = themes[(size_t) id - 1];
    else if (id >= 100 && id < 100 + saved.size()) chosen = themeNamed ("My: " + saved[id - 100]);
    else if (id == 200) chosen = themeNamed ("Custom");
    else return;
    edited = chosen;
    note = {};
    applyTheme (edited, true);
    pendingSave = false;
    refresh();
}
void ThemePanel::showStartFromMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("READY-MADE");
    const auto& themes = builtInThemes();
    for (int i = 0; i < (int) themes.size(); ++i)
        addItemWithRight (menu, i + 1, themes[(size_t) i].name, menuSwatches (themeSwatches (themes[(size_t) i])), true, edited.name == themes[(size_t) i].name);
    const auto saved = savedThemeNames();
    if (! saved.isEmpty())
    {
        menu.addSectionHeader ("MY THEMES");
        for (int j = 0; j < saved.size() && j < 99; ++j)
            addItemWithRight (menu, 100 + j, saved[j], menuSwatches (themeSwatches (themeNamed ("My: " + saved[j]))), true, edited.name == "My: " + saved[j]);
    }
    juce::SharedResourcePointer<TriadSettings> settings;
    if (settings->getCustomTheme().isNotEmpty() || edited.name == "Custom")
    {
        menu.addSeparator();
        addItemWithRight (menu, 200, "Custom", menuSwatches (themeSwatches (themeNamed ("Custom"))), true, edited.name == "Custom");
    }
    startFrom->open = true;
    startFrom->repaint();
    juce::Component::SafePointer<ThemePanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (startFrom.get()).withMinimumWidth (startFrom->getWidth()),
                        [safe] (int choice)
    {
        if (safe == nullptr) return;
        safe->startFrom->open = false;
        safe->startFrom->repaint();
        if (choice > 0) safe->startFromTheme (choice);
    });
}
ThemePanel::~ThemePanel() { picker->removeChangeListener (this); }

void ThemePanel::open()
{
    edited = before = currentTheme();
    random.setSeedRandomly();
    note = {};
    selected = 0;
    pendingSave = false;
    if (auto* parent = getParentComponent()) setBounds (parent->getLocalBounds());
    setVisible (true);
    toFront (true);
    refresh();
}
void ThemePanel::close()
{
    if (pendingSave) { pendingSave = false; applyTheme (edited, true); }
    stopTimer();
    setVisible (false);
}
void ThemePanel::cancel()
{
    pendingSave = false;
    stopTimer();
    applyTheme (before, true);
    setVisible (false);
}
void ThemePanel::randomise()
{
    note = "A random colourway, clear of the band colours: roll again, change any colour, or SAVE AS to name it";
    edit (randomTheme (random), false);
}
void ThemePanel::selectRole (int role)
{
    selected = juce::jlimit (0, Theme::numRoles - 1, role);
    refresh();
}
void ThemePanel::setRoleColour (int role, juce::Colour colour)
{
    role = juce::jlimit (0, Theme::numRoles - 1, role);
    auto t = edited;
    const auto wanted = colour.withAlpha (1.0f);
    t[role] = keepClearOfBands (role, wanted);
    note = {};
    if (t[role] != wanted)
    {
        const int band = nearestBand (wanted);
        note = isBackgroundRole (role) ? Theme::roleName (role) + " made darker so every band colour shows on it"
                                       : Theme::roleName (role) + " moved a little away from Band " + juce::String (band + 1) + "'s colour (band colours are kept for the bands)";
    }
    t.name = "Custom";
    selected = role;
    edit (t, false);
}
void ThemePanel::edit (const Theme& t, bool fromPicker)
{
    juce::ignoreUnused (fromPicker);
    edited = t;
    applyTheme (edited, false);   // shows at once in every window of this plug-in
    pendingSave = true;           // stored (for the other plug-ins) a moment later, not on every move of the picker
    if (! isTimerRunning()) startTimer (300);
    refresh();
}
void ThemePanel::timerCallback()
{
    stopTimer();
    if (pendingSave) { pendingSave = false; applyTheme (edited, true); }
}
void ThemePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (updating) return;
    const auto c = picker->getColour().withAlpha (1.0f);
    if (c.getARGB() != edited[selected].getARGB()) setRoleColour (selected, c);
}
void ThemePanel::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    for (int r = 0; r < (int) rows.size(); ++r) rows[(size_t) r]->update (edited[r], r == selected);
    picker->setColour (edited[selected]);
    startFrom->name = themeDisplayName (edited);
    startFrom->swatches = themeSwatches (edited);
    startFrom->repaint();
    // The buttons in the menus' look: DONE filled with the chosen colour.
    const auto m = menuColours();
    doneButton.setColour (juce::TextButton::buttonColourId, m.chosen);
    doneButton.setColour (juce::TextButton::textColourOffId, m.chosenInk);
    for (auto* b : { &saveButton, &cancelButton, &fixButton })
    {
        b->setColour (juce::TextButton::buttonColourId, currentPalette.controls.brighter (0.06f));
        b->setColour (juce::TextButton::textColourOffId, textColour);
    }
    const auto issues = readabilityProblems (edited);
    problems.setColour (juce::Label::textColourId, issues.isEmpty() ? dimTextColour : juce::Colour (0xffe8b35c));
    problems.setText (! issues.isEmpty() ? issues.joinIntoString ("; ") + "."
                                         : note.isNotEmpty() ? note + "."
                                                             : juce::String ("The theme you leave here is used by every Triad, Triad Time and Triad Split. "
                                                                             "Band colours never change; theme colours keep clear of them."),
                      juce::dontSendNotification);
    fixButton.setEnabled (! issues.isEmpty());
    repaint();
}

juce::Rectangle<int> ThemePanel::box() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (820, getWidth() - 20), juce::jmin (560, getHeight() - 16));
}
void ThemePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto m = menuColours();
    const auto frame = box().toFloat();
    g.setColour (m.back);
    g.fillRoundedRectangle (frame, 14.0f);
    g.setColour (m.edge);
    g.drawRoundedRectangle (frame.reduced (0.5f), 14.0f, 1.0f);
    auto top = box().reduced (24, 16).removeFromTop (34);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("COLOURS", top.removeFromLeft (112), juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText ("Triad, Triad Time and Triad Split share them", top.withTrimmedRight (startFromLabel.getWidth() + startFrom->getWidth()),
                juce::Justification::centredLeft);
    const auto caps = [&g] (const juce::String& text, juce::Rectangle<int> area, juce::Colour colour, juce::Justification j)
    {
        g.setColour (colour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.06f));
        g.drawText (text, area, j);
    };
    caps ("START FROM", startFromLabel, currentPalette.softText, juce::Justification::centredRight);
    caps ("THEME COLOURS", listHeader, m.header, juce::Justification::centredLeft);
    caps (Theme::roleName (selected).toUpperCase(), pickerHeader, m.header, juce::Justification::centredLeft);
    // The ten band colours, which no theme colour may come close to (squares, with their numbers).
    auto chips = chipsArea;
    caps ("BAND COLOURS (KEPT FOR THE BANDS)", chips.removeFromTop (18), m.header, juce::Justification::centredLeft);
    chips.removeFromTop (6);
    const int chip = juce::jmin (26, (chips.getWidth() - 9 * 8) / 10);
    for (int b = 0; b < 10; ++b)
    {
        const auto square = chips.removeFromLeft (chip).withHeight (chip).toFloat();
        chips.removeFromLeft (8);
        g.setColour (bandColour (b));
        g.fillRoundedRectangle (square, 5.0f);
        g.setColour (inkColour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (juce::String (b + 1), square, juce::Justification::centred);
    }
    g.setColour (m.line);
    g.fillRect (box().reduced (24, 0).getX(), dividerY, box().getWidth() - 48, 1);
}
void ThemePanel::resized()
{
    auto r = box().reduced (24, 16);
    auto top = r.removeFromTop (34);
    startFrom->setBounds (top.removeFromRight (210).reduced (0, 1));
    top.removeFromRight (12);
    startFromLabel = top.removeFromRight (90);
    r.removeFromTop (14);
    auto bottom = r.removeFromBottom (82);
    dividerY = bottom.getY() + 4;
    auto buttons = bottom.removeFromBottom (34);
    doneButton.setBounds (buttons.removeFromRight (96));
    buttons.removeFromRight (10);
    cancelButton.setBounds (buttons.removeFromRight (96));
    buttons.removeFromRight (10);
    saveButton.setBounds (buttons.removeFromRight (96));
    buttons.removeFromRight (10);
    saveName.setBounds (buttons.removeFromRight (juce::jmin (180, juce::jmax (100, buttons.getWidth() - 170))));
    fixButton.setBounds (buttons.removeFromLeft (150));
    bottom.removeFromBottom (6);
    problems.setBounds (bottom.withTrimmedTop (10).withTrimmedLeft (-4));
    auto list = r.removeFromLeft (juce::jmin (384, r.getWidth() / 2 - 10));
    r.removeFromLeft (32);
    listHeader = list.removeFromTop (22);
    chipsArea = list.removeFromBottom (52);
    list.removeFromBottom (8);
    const int rowHeight = juce::jmin (33, list.getHeight() / Theme::numRoles);
    for (auto& row : rows) row->setBounds (list.removeFromTop (rowHeight));
    pickerHeader = r.removeFromTop (22);
    randomButton->setBounds (r.removeFromBottom (38));
    r.removeFromBottom (12);
    picker->setBounds (r);
}
void ThemePanel::mouseDown (const juce::MouseEvent& e)
{
    if (! box().contains (e.getPosition())) close();   // a click outside the box closes it
}
}
