#include "Themes.h"

using namespace juce;

namespace cs
{
const std::vector<Theme>& themes()
{
    static const std::vector<Theme> list = []
    {
        std::vector<Theme> v;
        Theme t;

        // Crate Wood: record-crate pine, cream labels, bakelite chicken-head knobs
        t = {};
        t.name = "Crate Wood";
        t.bgTop = Colour (0xff6b4428); t.bgBottom = Colour (0xff3e2615);
        t.panel = Colour (0xffeee2c6); t.panelEdge = Colour (0xff9c7b4e);
        t.text = Colour (0xff2f2015); t.textDim = Colour (0xff7a6046);
        t.accent = Colour (0xffd2552e); t.accent2 = Colour (0xff2f6f68);
        t.knobFace = Colour (0xff1d1714); t.knobRim = Colour (0xffd9c79e); t.track = Colour (0x33000000);
        t.waveBg = Colour (0xff221812); t.wave = Colour (0xfff0dcb0); t.pad = Colour (0xffd8c9a6); t.padText = Colour (0xff2f2015);
        t.slices = { Colour (0xffd2552e), Colour (0xffe0a33b), Colour (0xff2f6f68), Colour (0xff7a8b3a), Colour (0xffa0496a), Colour (0xff4f6fa8) };
        t.knobStyle = 0; t.texture = 1; t.corner = 4.0f;
        v.push_back (t);

        // Concrete: grey cast panels, stencil type, safety-orange accents
        t = {};
        t.name = "Concrete";
        t.bgTop = Colour (0xff8d8d88); t.bgBottom = Colour (0xff6c6c68);
        t.panel = Colour (0xffb9b8b2); t.panelEdge = Colour (0xff57574f);
        t.text = Colour (0xff1e1e1c); t.textDim = Colour (0xff4b4b47);
        t.accent = Colour (0xffff6a13); t.accent2 = Colour (0xff1e1e1c);
        t.knobFace = Colour (0xff9a9993); t.knobRim = Colour (0xff3d3d39); t.track = Colour (0x40000000);
        t.waveBg = Colour (0xff2b2b29); t.wave = Colour (0xffdcdcd4); t.pad = Colour (0xffa4a39c); t.padText = Colour (0xff1e1e1c);
        t.slices = { Colour (0xffff6a13), Colour (0xfff2c230), Colour (0xffd8d8d0), Colour (0xff8aa0a8), Colour (0xffc05a2a) };
        t.knobStyle = 1; t.texture = 2; t.corner = 2.0f;
        v.push_back (t);

        // Neon Pawn: pawn-shop window at night, black glass, pink and green tubes
        t = {};
        t.name = "Neon Pawn";
        t.bgTop = Colour (0xff0b0712); t.bgBottom = Colour (0xff180b26);
        t.panel = Colour (0xe0120c1d); t.panelEdge = Colour (0xffff3fa4).withAlpha (0.5f);
        t.text = Colour (0xfff7eaff); t.textDim = Colour (0xffa38fbd);
        t.accent = Colour (0xffff3fa4); t.accent2 = Colour (0xff3dffb4);
        t.knobFace = Colour (0xff161022); t.knobRim = Colour (0xff2c2241); t.track = Colour (0xff2c2241);
        t.waveBg = Colour (0xff07040c); t.wave = Colour (0xff3dffb4); t.pad = Colour (0xff1c1429); t.padText = Colour (0xfff7eaff);
        t.slices = { Colour (0xffff3fa4), Colour (0xff3dffb4), Colour (0xfffff04a), Colour (0xff47c8ff), Colour (0xffb26bff) };
        t.knobStyle = 2; t.texture = 3; t.corner = 6.0f;
        v.push_back (t);

        // Cassette: off-white shell, label stripes, reel knobs
        t = {};
        t.name = "Cassette";
        t.bgTop = Colour (0xffe9e4d8); t.bgBottom = Colour (0xffd6cfbf);
        t.panel = Colour (0xfff7f3ea); t.panelEdge = Colour (0xff3a3a3a);
        t.text = Colour (0xff262626); t.textDim = Colour (0xff6d6a63);
        t.accent = Colour (0xffe8432d); t.accent2 = Colour (0xff1c8fbf);
        t.knobFace = Colour (0xff2a2a2a); t.knobRim = Colour (0xfff1ede4); t.track = Colour (0x30000000);
        t.waveBg = Colour (0xff3a3632); t.wave = Colour (0xfff4c542); t.pad = Colour (0xffeae4d6); t.padText = Colour (0xff262626);
        t.slices = { Colour (0xffe8432d), Colour (0xfff4c542), Colour (0xff1c8fbf), Colour (0xff7bb661) };
        t.knobStyle = 3; t.texture = 4; t.corner = 10.0f;
        v.push_back (t);
        return v;
    }();
    return list;
}

ShopLookAndFeel::ShopLookAndFeel() { setTheme (themes().front()); }

void ShopLookAndFeel::setTheme (const Theme& th)
{
    t = th;
    const bool lightPanel = t.panel.getPerceivedBrightness() > 0.55f && t.panel.getFloatAlpha() > 0.8f;
    const auto popupBg = lightPanel ? t.panel.brighter (0.05f) : t.bgTop.darker (0.3f).withAlpha (1.0f);
    const auto popupText = lightPanel ? t.text : t.text;
    setColour (ComboBox::backgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::textColourId, t.text);
    setColour (ComboBox::outlineColourId, t.panelEdge);
    setColour (ComboBox::arrowColourId, t.textDim);
    setColour (PopupMenu::backgroundColourId, popupBg);
    setColour (PopupMenu::textColourId, popupText);
    setColour (PopupMenu::headerTextColourId, t.accent);
    setColour (PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.4f));
    setColour (PopupMenu::highlightedTextColourId, lightPanel ? Colours::black : Colours::white);
    setColour (TextButton::textColourOffId, t.text);
    setColour (TextButton::textColourOnId, t.text);
    setColour (TextButton::buttonColourId, Colours::transparentBlack);
    setColour (Label::textColourId, t.text);
    setColour (TextEditor::backgroundColourId, popupBg);
    setColour (TextEditor::textColourId, popupText);
    setColour (TextEditor::outlineColourId, t.panelEdge);
    setColour (TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
    setColour (CaretComponent::caretColourId, t.accent);
    setColour (AlertWindow::backgroundColourId, popupBg);
    setColour (AlertWindow::textColourId, popupText);
    setColour (TooltipWindow::backgroundColourId, popupBg);
    setColour (TooltipWindow::textColourId, popupText);
}

void ShopLookAndFeel::paintBackdrop (Graphics& g, int w, int h) const
{
    g.setGradientFill (ColourGradient (t.bgTop, 0, 0, t.bgBottom, 0, (float) h, false));
    g.fillAll();
    Random rnd (11);
    switch (t.texture)
    {
        case 1:   // wood: wavy grain lines and plank seams
            for (int i = 0; i < 160; ++i)
            {
                const float y = rnd.nextFloat() * h;
                Path p; p.startNewSubPath (0, y);
                const float amp = 1.0f + rnd.nextFloat() * 4.0f, freq = 0.004f + rnd.nextFloat() * 0.01f, ph = rnd.nextFloat() * 6.0f;
                for (int x = 0; x <= w; x += 12) p.lineTo ((float) x, y + amp * std::sin (x * freq + ph));
                g.setColour ((rnd.nextBool() ? Colours::black : Colour (0xffffd9a0)).withAlpha (0.05f + 0.07f * rnd.nextFloat()));
                g.strokePath (p, PathStrokeType (0.6f + rnd.nextFloat() * 1.4f));
            }
            g.setColour (Colours::black.withAlpha (0.35f));
            for (int y = 0; y < h; y += h / 4) g.fillRect (0, y, w, 2);
            break;
        case 2:   // concrete: speckle, form-tie holes
            for (int i = 0; i < w * h / 9; ++i)
            {
                g.setColour ((rnd.nextBool() ? Colours::white : Colours::black).withAlpha (0.03f + 0.09f * rnd.nextFloat()));
                const float s = rnd.nextFloat() < 0.05f ? 2.5f : 1.0f;
                g.fillEllipse (rnd.nextFloat() * w, rnd.nextFloat() * h, s, s);
            }
            for (int x = 40; x < w; x += 260)
                for (int y = 30; y < h; y += 220)
                {
                    g.setColour (Colours::black.withAlpha (0.25f)); g.fillEllipse ((float) x, (float) y, 9, 9);
                    g.setColour (Colours::white.withAlpha (0.15f)); g.drawEllipse ((float) x - 1, (float) y - 1, 11, 11, 1.0f);
                }
            break;
        case 3:   // neon: perspective grid and glow
            g.setColour (t.accent2.withAlpha (0.07f));
            for (int x = 0; x < w; x += 28) g.drawVerticalLine (x, 0, (float) h);
            for (int y = 0; y < h; y += 28) g.drawHorizontalLine (y, 0, (float) w);
            g.setGradientFill (ColourGradient (t.accent.withAlpha (0.22f), w * 0.85f, 0, t.accent.withAlpha (0.0f), w * 0.85f - 500, 500, true));
            g.fillAll();
            break;
        case 4:   // cassette: label stripes along the top
        {
            const Colour stripes[] = { t.accent, Colour (0xfff08a2c), Colour (0xfff4c542), t.accent2 };
            for (int i = 0; i < 4; ++i) { g.setColour (stripes[i].withAlpha (0.85f)); g.fillRect (0, 4 + i * 5, w, 4); }
            for (int i = 0; i < w * h / 30; ++i)
            {
                g.setColour (Colours::black.withAlpha (0.025f * rnd.nextFloat()));
                g.fillRect (rnd.nextFloat() * w, rnd.nextFloat() * h, 1.0f, 1.0f);
            }
            break;
        }
        default: break;
    }
}

void ShopLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float r = d * 0.5f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (start + end) * 0.5f : start;
    const auto acc = s.isEnabled() ? t.accent : t.textDim;
    auto arc = [&] (float rad, float a0, float a1, Colour col, float thick)
    {
        Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, jmin (a0, a1), jmax (a0, a1), true);
        g.setColour (col);
        g.strokePath (p, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::rounded));
    };

    switch (t.knobStyle)
    {
        case 1:   // flat concrete: square-ish cap, painted line
        {
            arc (r - 2, start, end, t.track, 3.0f);
            arc (r - 2, from, angle, acc, 3.0f);
            auto face = Rectangle<float> (d * 0.66f, d * 0.66f).withCentre (c);
            g.setColour (Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (face.translated (1.5f, 2.0f), d * 0.12f);
            g.setColour (t.knobFace); g.fillRoundedRectangle (face, d * 0.12f);
            g.setColour (t.knobRim); g.drawRoundedRectangle (face, d * 0.12f, 1.2f);
            g.setColour (t.accent);
            g.drawLine (Line<float> (c.getPointOnCircumference (d * 0.08f, angle), c.getPointOnCircumference (d * 0.3f, angle)), 3.0f);
            break;
        }
        case 2:   // neon ring with glow
        {
            arc (r - 3, start, end, t.track, 4.0f);
            if (s.isEnabled()) for (int i = 3; i >= 1; --i) arc (r - 3, from, angle, acc.withAlpha (0.12f), 4.0f + i * 4.0f);
            arc (r - 3, from, angle, acc, 3.0f);
            auto face = Rectangle<float> (d * 0.6f, d * 0.6f).withCentre (c);
            g.setColour (t.knobFace); g.fillEllipse (face);
            g.setColour (t.accent2.withAlpha (0.6f)); g.drawEllipse (face, 1.2f);
            g.setColour (t.accent2);
            g.drawLine (Line<float> (c.getPointOnCircumference (d * 0.1f, angle), c.getPointOnCircumference (d * 0.28f, angle)), 2.2f);
            break;
        }
        case 3:   // cassette reel: hub with teeth that turn
        {
            arc (r - 2, start, end, t.track, 3.0f);
            arc (r - 2, from, angle, acc, 3.0f);
            auto reel = Rectangle<float> (d * 0.72f, d * 0.72f).withCentre (c);
            g.setColour (t.knobRim); g.fillEllipse (reel);
            g.setColour (t.knobFace); g.drawEllipse (reel, 1.5f);
            auto hub = reel.reduced (d * 0.16f);
            g.fillEllipse (hub);
            g.setColour (t.knobRim);
            for (int i = 0; i < 6; ++i)
            {
                const float a = angle + i * MathConstants<float>::twoPi / 6.0f;
                g.fillEllipse (Rectangle<float> (d * 0.06f, d * 0.06f).withCentre (c.getPointOnCircumference (hub.getWidth() * 0.32f, a)));
            }
            g.setColour (t.accent);
            g.fillEllipse (Rectangle<float> (d * 0.09f, d * 0.09f).withCentre (c.getPointOnCircumference (reel.getWidth() * 0.43f, angle)));
            break;
        }
        default:  // wooden-crate chicken-head pointer on a cream skirt
        {
            for (int i = 0; i <= 10; ++i)
            {
                const float a = start + (end - start) * i / 10.0f;
                g.setColour (t.text.withAlpha (i % 5 == 0 ? 0.75f : 0.4f));
                g.drawLine (Line<float> (c.getPointOnCircumference (r - 1, a), c.getPointOnCircumference (r - (i % 5 == 0 ? 6.0f : 4.0f), a)), 1.2f);
            }
            auto skirt = Rectangle<float> (d * 0.74f, d * 0.74f).withCentre (c);
            g.setColour (Colours::black.withAlpha (0.35f)); g.fillEllipse (skirt.translated (1.5f, 2.5f));
            g.setGradientFill (ColourGradient (t.knobRim.brighter (0.2f), skirt.getX(), skirt.getY(), t.knobRim.darker (0.3f), skirt.getRight(), skirt.getBottom(), false));
            g.fillEllipse (skirt);
            Path head;
            head.addRoundedRectangle (-d * 0.09f, -d * 0.40f, d * 0.18f, d * 0.62f, d * 0.08f);
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillPath (head, AffineTransform::rotation (angle).translated (c.translated (1.0f, 2.0f)));
            g.setColour (t.knobFace);
            g.fillPath (head, AffineTransform::rotation (angle).translated (c));
            g.setColour (acc);
            g.drawLine (Line<float> (c.getPointOnCircumference (d * 0.18f, angle), c.getPointOnCircumference (d * 0.36f, angle)), 2.0f);
            break;
        }
    }
}

void ShopLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const float h = jmin (r.getHeight(), 18.0f);
    auto led = Rectangle<float> (h - 4, h - 4).withPosition (r.getX() + 2, r.getCentreY() - (h - 4) * 0.5f);
    if (on && t.knobStyle == 2) { g.setColour (t.accent.withAlpha (0.3f)); g.fillEllipse (led.expanded (3)); }
    g.setColour (on ? t.accent : t.track);
    g.fillRoundedRectangle (led, t.corner * 0.4f);
    g.setColour (t.panelEdge.withAlpha (highlighted ? 1.0f : 0.7f));
    g.drawRoundedRectangle (led, t.corner * 0.4f, 1.0f);
    g.setColour (t.text);
    g.setFont (Font (FontOptions (12.0f, Font::bold)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (h + 4).toNearestInt(), Justification::centredLeft);
}

void ShopLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    auto fill = on ? t.accent.withAlpha (0.35f) : t.panel;
    if (down) fill = fill.darker (0.15f); else if (highlighted) fill = fill.brighter (0.08f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, jmin (t.corner, r.getHeight() * 0.5f));
    g.setColour (highlighted || on ? t.accent : t.panelEdge);
    g.drawRoundedRectangle (r, jmin (t.corner, r.getHeight() * 0.5f), 1.2f);
}

void ShopLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (t.panel.withMultipliedAlpha (box.isMouseOver() ? 1.0f : 0.9f).brighter (box.isMouseOver() ? 0.06f : 0.0f));
    g.fillRoundedRectangle (r, jmin (t.corner * 0.6f, r.getHeight() * 0.5f));
    g.setColour (t.panelEdge);
    g.drawRoundedRectangle (r, jmin (t.corner * 0.6f, r.getHeight() * 0.5f), 1.0f);
    Path arrow;
    const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (t.textDim);
    g.fillPath (arrow);
}

Font ShopLookAndFeel::getComboBoxFont (ComboBox&) { return Font (FontOptions (12.5f)); }
Font ShopLookAndFeel::getTextButtonFont (TextButton&, int h) { return Font (FontOptions (jmin (14.0f, (float) h * 0.55f), Font::bold)); }
Font ShopLookAndFeel::getPopupMenuFont() { return Font (FontOptions (14.0f)); }
Font ShopLookAndFeel::getLabelFont (Label& l) { return Font (FontOptions ((float) jmin (14, l.getHeight() - 4), Font::bold)); }
Font ShopLookAndFeel::getSliderPopupFont (Slider&) { return Font (FontOptions (13.0f, Font::bold)); }

void ShopLookAndFeel::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
{
    g.setColour (findColour (TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (body, 4.0f);
    g.setColour (t.accent);
    g.drawRoundedRectangle (body, 4.0f, 1.0f);
}
} // namespace cs
