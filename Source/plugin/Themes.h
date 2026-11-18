// UI themes. Each one changes the textures and knob drawing, not only the colours.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cs
{
struct Theme
{
    juce::String name;
    juce::Colour bgTop, bgBottom, panel, panelEdge, text, textDim, accent, accent2, knobFace, knobRim, track;
    juce::Colour waveBg, wave, pad, padText;
    std::vector<juce::Colour> slices;   // slice region colours, cycled
    int knobStyle = 0;                  // 0 wooden chicken-head, 1 flat concrete, 2 neon ring, 3 cassette reel
    int texture = 0;                    // 0 none, 1 wood grain, 2 concrete speckle, 3 neon grid, 4 cassette stripes
    float corner = 6.0f;
};

const std::vector<Theme>& themes();

class ShopLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShopLookAndFeel();
    void setTheme (const Theme&);
    const Theme& theme() const { return t; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
    juce::Font getSliderPopupFont (juce::Slider&) override;

    // paints the theme's background texture into an image of the given size
    void paintBackdrop (juce::Graphics&, int w, int h) const;

private:
    Theme t;
};
} // namespace cs
