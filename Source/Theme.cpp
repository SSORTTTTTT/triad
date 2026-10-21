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
void addThemeItems (juce::PopupMenu& menu, int firstId)
{
    menu.addSectionHeader ("THEME");
    const auto& current = currentTheme();
    int i = 0;
    for (const auto& t : builtInThemes()) menu.addItem (firstId + i++, t.name, true, current.name == t.name);
    const auto saved = savedThemeNames();
    if (! saved.isEmpty())
    {
        juce::PopupMenu mine;
        for (int j = 0; j < saved.size() && j < 99; ++j) mine.addItem (firstId + 100 + j, saved[j], true, current.name == "My: " + saved[j]);
        menu.addSubMenu ("My themes", mine, true, nullptr, current.name.startsWith ("My: "));
    }
    juce::SharedResourcePointer<TriadSettings> settings;
    if (settings->getCustomTheme().isNotEmpty() || current.name == "Custom") menu.addItem (firstId + 200, "Custom", true, current.name == "Custom");
    menu.addItem (firstId + 201, "Edit colours...");
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

// ---- Edit colours -------------------------------------------------------------------------------------------------------
// RANDOM COLOURWAY: a dice and the words.
struct ThemePanel::RandomButton final : public juce::Button
{
    RandomButton() : Button ("Random colourway") { setTooltip ("A random colour scheme: clear of the band colours, readable. Roll again, change any colour, or SAVE AS to keep it"); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (currentPalette.controlsOn.brighter (down ? 0.2f : over ? 0.1f : 0.0f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        const float size = r.getHeight() * 0.56f;
        auto content = r.withSizeKeepingCentre (size + 8.0f + 150.0f, r.getHeight());
        IconButton::drawIcon (g, IconButton::Icon::dice, content.removeFromLeft (size).withSizeKeepingCentre (size, size), textColour);
        content.removeFromLeft (8.0f);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("RANDOM COLOURWAY", content, juce::Justification::centredLeft);
    }
};
struct ThemePanel::Row final : public juce::Component
{
    Row (ThemePanel& p, int r) : owner (p), role (r)
    {
        hex.setInputRestrictions (7, "#0123456789abcdefABCDEF");
        hex.setFont (juce::FontOptions (12.5f));
        hex.setIndents (6, 0);
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
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        if (chosen) { g.setColour (currentPalette.controlsOn); g.fillRoundedRectangle (r, 5.0f); }
        const auto swatch = r.withWidth (r.getHeight()).reduced (4.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (swatch, 4.0f);
        g.setColour (textColour.withAlpha (0.45f));
        g.drawRoundedRectangle (swatch.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (chosen ? textColour : dimTextColour);
        g.setFont (juce::FontOptions (12.5f, chosen ? juce::Font::bold : juce::Font::plain));
        g.drawText (Theme::roleName (role), r.withTrimmedLeft (r.getHeight() + 4.0f).withTrimmedRight (120.0f), juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 3);
        reset.setBounds (r.removeFromRight (26));
        r.removeFromRight (6);
        hex.setBounds (r.removeFromRight (82));
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
    picker.addChangeListener (this);
    addAndMakeVisible (picker);
    startFrom.setTooltip ("Start from a ready-made or saved theme");
    startFrom.onChange = [this]
    {
        if (updating) return;
        const int id = startFrom.getSelectedId();
        const auto& themes = builtInThemes();
        Theme chosen;
        if (id >= 1 && id <= (int) themes.size()) chosen = themes[(size_t) id - 1];
        else if (id >= 100 && id < 200) chosen = themeNamed ("My: " + startFrom.getItemText (startFrom.indexOfItemId (id)));
        else if (id == 200) chosen = themeNamed ("Custom");
        else return;
        edited = chosen;
        note = {};
        applyTheme (edited, true);
        pendingSave = false;
        refresh();
    };
    addAndMakeVisible (startFrom);
    saveName.setTextToShowWhenEmpty ("Name", dimTextColour);
    saveName.setFont (juce::FontOptions (12.5f));
    saveName.setIndents (6, 0);
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
        problems.setText ("Saved as \"" + name + "\" (gear menu > My themes)", juce::dontSendNotification);
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
    problems.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (problems);
}
ThemePanel::~ThemePanel() { picker.removeChangeListener (this); }

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
    const auto c = picker.getCurrentColour().withAlpha (1.0f);
    if (c.getARGB() != edited[selected].getARGB()) setRoleColour (selected, c);
}
void ThemePanel::refresh()
{
    const juce::ScopedValueSetter<bool> busy (updating, true);
    for (int r = 0; r < (int) rows.size(); ++r) rows[(size_t) r]->update (edited[r], r == selected);
    picker.setCurrentColour (edited[selected], juce::dontSendNotification);
    picker.setColour (juce::ColourSelector::backgroundColourId, juce::Colours::transparentBlack);
    picker.setColour (juce::ColourSelector::labelTextColourId, dimTextColour);
    // Start from: the ready-made themes, the saved ones, Custom.
    startFrom.clear (juce::dontSendNotification);
    const auto& themes = builtInThemes();
    for (int i = 0; i < (int) themes.size(); ++i) startFrom.addItem (themes[(size_t) i].name, i + 1);
    const auto saved = savedThemeNames();
    if (! saved.isEmpty())
    {
        startFrom.addSectionHeading ("My themes");
        for (int j = 0; j < saved.size() && j < 99; ++j) startFrom.addItem (saved[j], 100 + j);
    }
    juce::SharedResourcePointer<TriadSettings> settings;
    if (settings->getCustomTheme().isNotEmpty() || edited.name == "Custom") startFrom.addItem ("Custom", 200);
    int id = 0;
    for (int i = 0; i < (int) themes.size(); ++i) if (edited.name == themes[(size_t) i].name) id = i + 1;
    if (edited.name.startsWith ("My: ")) id = 100 + saved.indexOf (edited.name.substring (4));
    if (edited.name == "Custom") id = 200;
    startFrom.setSelectedId (id, juce::dontSendNotification);
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
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (760, getWidth() - 20), juce::jmin (470, getHeight() - 16));
}
void ThemePanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto frame = box().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (frame, 8.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (frame.reduced (0.5f), 8.0f, 1.2f);
    auto top = box().reduced (18, 14).removeFromTop (30);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ("COLOURS", top.removeFromLeft (100), juce::Justification::centredLeft);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Triad, Triad Time and Triad Split share them", top.removeFromLeft (300), juce::Justification::centredLeft);
    g.drawText ("Start from", top.withTrimmedRight (226).removeFromRight (80), juce::Justification::centredRight);
    // The ten band colours, which no theme colour may come close to.
    auto chips = bandChipArea();
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("BAND COLOURS (KEPT FOR THE BANDS)", chips.removeFromTop (16), juce::Justification::centredLeft);
    chips.removeFromTop (4);
    const int chipWidth = juce::jmin (30, (chips.getWidth() - 9 * 4) / 10);
    for (int b = 0; b < 10; ++b)
    {
        const auto chip = chips.removeFromLeft (chipWidth).withHeight (16).toFloat();
        chips.removeFromLeft (4);
        g.setColour (bandColour (b));
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (inkColour);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (juce::String (b + 1), chip, juce::Justification::centred);
    }
}
juce::Rectangle<int> ThemePanel::bandChipArea() const
{
    auto r = box().reduced (18, 14);
    r.removeFromTop (40);
    r.removeFromBottom (66);
    return r.removeFromLeft (370).removeFromBottom (40);
}
void ThemePanel::resized()
{
    auto r = box().reduced (18, 14);
    auto top = r.removeFromTop (30);
    startFrom.setBounds (top.removeFromRight (220).reduced (0, 2));
    r.removeFromTop (10);
    auto bottom = r.removeFromBottom (66);
    auto buttons = bottom.removeFromBottom (30);
    doneButton.setBounds (buttons.removeFromRight (84));
    buttons.removeFromRight (6);
    cancelButton.setBounds (buttons.removeFromRight (84));
    buttons.removeFromRight (14);
    saveButton.setBounds (buttons.removeFromRight (84));
    buttons.removeFromRight (6);
    saveName.setBounds (buttons.removeFromRight (160));
    fixButton.setBounds (buttons.removeFromLeft (130));
    bottom.removeFromBottom (6);
    problems.setBounds (bottom);
    auto list = r.removeFromLeft (370);
    list.removeFromBottom (46);   // the band colours
    r.removeFromLeft (18);
    const int rowHeight = juce::jmin (32, list.getHeight() / Theme::numRoles);
    for (auto& row : rows) row->setBounds (list.removeFromTop (rowHeight));
    randomButton->setBounds (r.removeFromBottom (34));
    r.removeFromBottom (10);
    picker.setBounds (r);
}
void ThemePanel::mouseDown (const juce::MouseEvent& e)
{
    if (! box().contains (e.getPosition())) close();   // a click outside the box closes it
}
}
