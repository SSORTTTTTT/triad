#include "PluginSearch.h"

namespace triad::search
{
using namespace triad::ui;

namespace
{
juce::String compact (const juce::String& s)
{
    juce::String out;
    for (auto c : s.toLowerCase())
        if (juce::CharacterFunctions::isLetterOrDigit (c)) out += c;
    return out;
}
juce::StringArray wordsOf (const juce::String& s)
{
    juce::StringArray words;
    words.addTokens (s.toLowerCase(), " -_./()[]:", "");
    words.removeEmptyStrings();
    return words;
}
// How well one search word fits a plug-in (lower is better; -1: not at all).
int rankWord (const juce::String& word, const juce::String& name, const juce::StringArray& nameWords, const juce::String& maker,
              const juce::StringArray& makerWords)
{
    if (name == word) return 0;
    if (name.startsWith (word)) return 1;
    for (const auto& w : nameWords) if (w.startsWith (word)) return 2;
    if (name.contains (word)) return 3;
    const auto c = compact (word);
    if (c.isNotEmpty() && compact (name).contains (c)) return 4;
    if (maker.startsWith (word)) return 5;
    for (const auto& w : makerWords) if (w.startsWith (word)) return 5;
    if (maker.contains (word) || (c.isNotEmpty() && compact (maker).contains (c))) return 6;
    return -1;
}
int formatOrder (const juce::String& format) { return format == "VST3" ? 1 : format == "AU" ? 2 : 0; }   // built in first
}

std::vector<int> match (const std::vector<EffectEntry>& entries, const juce::String& query, const std::function<bool (const EffectEntry&)>& isFavourite)
{
    const auto q = query.trim().toLowerCase();
    juce::StringArray words;
    words.addTokens (q, " ", "");
    words.removeEmptyStrings();
    struct Hit { int index, rank; bool favourite; };
    std::vector<Hit> hits;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (e.crashed) continue;
        int rank = 0;
        if (! words.isEmpty())
        {
            const auto name = e.name.toLowerCase(), maker = e.manufacturer.toLowerCase();
            const auto nameWords = wordsOf (e.name), makerWords = wordsOf (e.manufacturer);
            // The whole query first ("pro q" is "Pro-Q 3"), then word by word.
            rank = words.size() > 1 ? rankWord (q, name, nameWords, maker, makerWords) : -1;
            if (rank < 0)
            {
                for (const auto& w : words)
                {
                    const int r = rankWord (w, name, nameWords, maker, makerWords);
                    if (r < 0) { rank = -1; break; }
                    rank = juce::jmax (rank, r);
                }
            }
        }
        if (rank >= 0) hits.push_back ({ i, rank, isFavourite != nullptr && isFavourite (e) });
    }
    std::stable_sort (hits.begin(), hits.end(), [&entries] (const Hit& a, const Hit& b)
    {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.favourite != b.favourite) return a.favourite;
        const auto& ea = entries[(size_t) a.index];
        const auto& eb = entries[(size_t) b.index];
        if (const int byName = ea.name.compareIgnoreCase (eb.name); byName != 0) return byName < 0;
        if (ea.verified != eb.verified) return ea.verified;
        return formatOrder (ea.format) < formatOrder (eb.format);
    });
    std::vector<int> out;
    out.reserve (hits.size());
    for (const auto& h : hits) out.push_back (h.index);
    return out;
}

void drawMagnifier (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto s = juce::jmin (area.getWidth(), area.getHeight());
    const auto r = area.withSizeKeepingCentre (s, s);
    const auto lens = juce::Rectangle<float> (r.getX() + s * 0.08f, r.getY() + s * 0.08f, s * 0.6f, s * 0.6f);
    g.setColour (colour);
    g.drawEllipse (lens, juce::jmax (1.4f, s * 0.11f));
    juce::Path handle;
    const auto c = lens.getCentre();
    const auto a = c + juce::Point<float> (lens.getWidth() * 0.36f, lens.getHeight() * 0.36f);
    handle.startNewSubPath (a);
    handle.lineTo (r.getRight() - s * 0.06f, r.getBottom() - s * 0.06f);
    g.strokePath (handle, juce::PathStrokeType (juce::jmax (1.8f, s * 0.15f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---- The menu item --------------------------------------------------------------------------------------
MenuItem::MenuItem (std::function<void (const juce::String&)> openSearch) : CustomComponent (true), open (std::move (openSearch)) {}
MenuItem::~MenuItem()
{
    if (window != nullptr) window->removeKeyListener (this);
}
void MenuItem::parentHierarchyChanged()
{
    auto* top = getTopLevelComponent();
    if (top == this || top == window.getComponent()) return;
    if (window != nullptr) window->removeKeyListener (this);
    window = top;
    if (window != nullptr) window->addKeyListener (this);
}
bool MenuItem::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    // Typing (letters, digits) while the effect menu is open: close it and search. (Not Cmd+F: left to the DAW.)
    const auto c = key.getTextCharacter();
    const bool typed = ! key.getModifiers().isCommandDown() && ! key.getModifiers().isCtrlDown() && ! key.getModifiers().isAltDown()
                       && juce::CharacterFunctions::isLetterOrDigit (c);
    if (! typed || opening) return false;
    opening = true;
    const auto text = juce::String::charToString (c);
    auto callback = open;
    juce::MessageManager::callAsync ([callback, text]
    {
        juce::PopupMenu::dismissAllActiveMenus();
        if (callback) callback (text);
    });
    return true;
}
void MenuItem::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (8, 3);
    if (isItemHighlighted())
    {
        g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f, 1.0f), 4.0f);
    }
    g.setColour (currentPalette.field);
    g.fillRoundedRectangle (r.toFloat(), 5.0f);
    g.setColour (isItemHighlighted() ? accent : currentPalette.fieldOutline);
    g.drawRoundedRectangle (r.toFloat(), 5.0f, 1.0f);
    auto inside = r.reduced (8, 0);
    drawMagnifier (g, inside.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f), accent);
    inside.removeFromLeft (8);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (dimTextColour);
    g.drawText ("or just type", inside.removeFromRight (80), juce::Justification::centredRight);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (textColour);
    g.drawText ("Search plug-ins...", inside, juce::Justification::centredLeft);
}

// ---- The panel -------------------------------------------------------------------------------------------
Panel::Panel()
{
    setInterceptsMouseClicks (true, true);
    setWantsKeyboardFocus (false);
    box.setFont (juce::FontOptions (16.0f));
    box.setIndents (34, 0);
    box.setJustification (juce::Justification::centredLeft);
    box.setTextToShowWhenEmpty ("Search plug-ins by name or maker", dimTextColour);
    box.setSelectAllWhenFocused (false);
    box.onTextChange = [this] { update(); };
    box.onReturnKey = [this] { chooseSelected(); };
    box.onEscapeKey = [this] { close(); };
    box.addKeyListener (this);
    addAndMakeVisible (box);
    rows.setRowHeight (30);
    rows.setOutlineThickness (0);
    rows.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    rows.setWantsKeyboardFocus (false);
    rows.setMouseMoveSelectsRows (false);
    addAndMakeVisible (rows);
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);
}
Panel::~Panel() { box.removeKeyListener (this); }

void Panel::open (std::shared_ptr<std::vector<EffectEntry>> entries, const juce::String& target, const juce::String& initialText,
                  std::function<bool (const EffectEntry&)> isFavourite, std::function<void (int)> choose)
{
    list = std::move (entries);
    targetText = target;
    favourite = std::move (isFavourite);
    onChoose = std::move (choose);
    if (auto* parent = getParentComponent()) setBounds (parent->getLocalBounds());
    box.setTextToShowWhenEmpty ("Search plug-ins by name or maker", dimTextColour);
    box.setText (initialText, false);
    box.moveCaretToEnd();
    update();
    setVisible (true);
    toFront (true);
    if (isShowing()) box.grabKeyboardFocus();
    repaint();
}
void Panel::close()
{
    setVisible (false);
    onChoose = nullptr;
}
void Panel::setQuery (const juce::String& text)
{
    box.setText (text, false);
    update();
}
void Panel::update()
{
    results = list != nullptr ? match (*list, box.getText(), favourite) : std::vector<int>();
    rows.updateContent();
    rows.selectRow (results.empty() ? -1 : 0);
    rows.scrollToEnsureRowIsOnscreen (0);
    repaint();
}
int Panel::getSelectedResult() const
{
    const int row = rows.getSelectedRow();
    return row >= 0 && row < (int) results.size() ? row : -1;
}
void Panel::selectResult (int row)
{
    if (results.empty()) return;
    row = juce::jlimit (0, (int) results.size() - 1, row);
    rows.selectRow (row);
    rows.scrollToEnsureRowIsOnscreen (row);
}
bool Panel::chooseSelected()
{
    const int row = getSelectedResult();
    if (row < 0 || list == nullptr) return false;
    const int index = results[(size_t) row];
    if (! (*list)[(size_t) index].verified && (*list)[(size_t) index].crashed) return false;
    auto callback = onChoose;
    close();
    if (callback) callback (index);
    return true;
}
bool Panel::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const int row = getSelectedResult();
    const int page = juce::jmax (1, rows.getHeight() / rows.getRowHeight() - 1);
    if (key.isKeyCode (juce::KeyPress::downKey))     { selectResult (row + 1); return true; }
    if (key.isKeyCode (juce::KeyPress::upKey))       { selectResult (row - 1); return true; }
    if (key.isKeyCode (juce::KeyPress::pageDownKey)) { selectResult (row + page); return true; }
    if (key.isKeyCode (juce::KeyPress::pageUpKey))   { selectResult (row - page); return true; }
    return false;
}
void Panel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    rows.selectRow (row);
    box.grabKeyboardFocus();
}
void Panel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    rows.selectRow (row);
    chooseSelected();
}
void Panel::mouseDown (const juce::MouseEvent& e)
{
    if (! frame().contains (e.getPosition())) close();
}
juce::Rectangle<int> Panel::frame() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (600, getWidth() - 40), juce::jmin (470, getHeight() - 40));
}
void Panel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto f = frame().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (f, 8.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (f, 8.0f, 1.2f);
    auto inside = frame().reduced (18, 14);
    auto title = inside.removeFromTop (20);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (textColour);
    g.drawText ("SEARCH PLUG-INS", title, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (dimTextColour);
    g.drawText ("into " + targetText, title.withTrimmedLeft (150).withTrimmedRight (90), juce::Justification::centredLeft);
    // How many match, and the keys.
    auto footer = frame().reduced (18, 10).removeFromBottom (18);
    const int total = list != nullptr ? (int) std::count_if (list->begin(), list->end(), [] (const EffectEntry& e) { return ! e.crashed; }) : 0;
    g.setFont (juce::FontOptions (11.5f));
    g.setColour (dimTextColour);
    g.drawText (box.getText().trim().isEmpty() ? juce::String (total) + " plug-ins" : juce::String ((int) results.size()) + " of " + juce::String (total) + " plug-ins",
                footer, juce::Justification::centredLeft);
    g.drawText (u8 ("\xe2\x86\x91 \xe2\x86\x93  choose    Return  load    Esc  close"), footer, juce::Justification::centredRight);
    if (results.empty() && box.getText().trim().isNotEmpty())
    {
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No plug-in called \"" + box.getText().trim() + "\"", rows.getBounds().removeFromTop (60), juce::Justification::centred);
    }
}
void Panel::paintOverChildren (juce::Graphics& g)
{
    drawMagnifier (g, box.getBounds().toFloat().removeFromLeft (34.0f).withSizeKeepingCentre (16.0f, 16.0f), accent);
}
void Panel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (list == nullptr || row < 0 || row >= (int) results.size()) return;
    const auto& e = (*list)[(size_t) results[(size_t) row]];
    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);
    if (selected)
    {
        g.setColour (currentPalette.controlsOn);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (r.toFloat().removeFromLeft (3.0f), 1.5f);
    }
    r.reduce (10, 0);
    // The format, right.
    const auto format = e.format == "VST3" || e.format == "AU" ? e.format : juce::String ("BUILT IN");
    auto chip = r.removeFromRight (64).withSizeKeepingCentre (64, 18);
    g.setColour (currentPalette.pill);
    g.fillRoundedRectangle (chip.toFloat(), 9.0f);
    g.setColour (dimTextColour);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (format, chip, juce::Justification::centred);
    if (! e.verified)
    {
        g.setFont (juce::FontOptions (10.5f));
        g.drawText ("not checked", r.removeFromRight (80), juce::Justification::centredRight);
    }
    // A star for a favourite.
    if (favourite != nullptr && favourite (e))
    {
        juce::Path star;
        const auto c = r.removeFromLeft (16).toFloat().getCentre();
        star.addStar (c, 5, 2.6f, 6.4f, -juce::MathConstants<float>::halfPi * 0.0f);
        g.setColour (accent);
        g.fillPath (star);
        r.removeFromLeft (6);
    }
    // The name, the part that matches in the accent colour, then the maker.
    const auto query = box.getText().trim();
    juce::AttributedString text;
    const juce::FontOptions nameFont (14.0f, juce::Font::bold);
    const int at = query.isNotEmpty() ? e.name.indexOfIgnoreCase (query) : -1;
    const auto nameColour = e.verified ? textColour : dimTextColour;
    if (at >= 0)
    {
        text.append (e.name.substring (0, at), nameFont, nameColour);
        text.append (e.name.substring (at, at + query.length()), nameFont, accent);
        text.append (e.name.substring (at + query.length()), nameFont, nameColour);
    }
    else text.append (e.name, nameFont, nameColour);
    if (e.manufacturer.isNotEmpty()) text.append ("   " + e.manufacturer, juce::FontOptions (12.5f), dimTextColour);
    text.setJustification (juce::Justification::centredLeft);
    text.setWordWrap (juce::AttributedString::none);
    text.draw (g, r.toFloat());
}
void Panel::resized()
{
    auto inside = frame().reduced (18, 14);
    auto title = inside.removeFromTop (20);
    closeButton.setBounds (title.removeFromRight (80).withSizeKeepingCentre (80, 24));
    inside.removeFromTop (10);
    box.setBounds (inside.removeFromTop (36));
    inside.removeFromTop (10);
    inside.removeFromBottom (24);
    rows.setBounds (inside);
}
}
