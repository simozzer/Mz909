// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

#include <memory>
#include <vector>

//
// The look of the panel: small knobs with a 0-10 scale round them, and a pad for each of
// the eleven sounds.
//
class PanelLook final : public juce::LookAndFeel_V4
{
public:
    PanelLook();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return juce::FontOptions(11.5f, juce::Font::bold);
    }
};

//
// MIDI learn, from the panel: right-click a knob for its menu, and its hover hint names
// the CC it answers to now.
//
namespace midiLearn
{
    void showMenu(Mz909Processor&, int index);
    juce::String hint(const Mz909Processor&, int index);
}

// A slider whose right button opens the MIDI learn menu instead of moving it, and whose hint
// is worked out when shown, so it names the CC as it is now.
class LearnSlider final : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    std::function<juce::String()> hint;

    juce::String getTooltip() override { return hint ? hint() : juce::String(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) Slider::mouseDrag(e); }
    void mouseUp(const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) Slider::mouseUp(e); }
};

// One knob, with its legend beneath.
class PanelKnob final : public juce::Component, public juce::TooltipClient
{
public:
    PanelKnob(Mz909Processor&, const juce::String& paramId, const juce::String& legend, juce::Colour cap);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;   // right-click on the legend, too

    juce::String getTooltip() override { return midiLearn::hint(processor, index); }

private:
    Mz909Processor& processor;
    int index;
    LearnSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String legend;
};

//
// A pad: plays its sound when pressed - on the press, as a drum should, not the release -
// and lights whenever the sound is struck, from the pad, the host or a controller.
//
class Pad final : public juce::Component, public juce::TooltipClient
{
public:
    Pad(int voice, juce::Colour colour);

    std::function<void(bool shift)> onStrike;

    void light() { litUntil = juce::Time::getMillisecondCounter() + 110; repaint(); }
    void tick();   // from the panel's timer: lets the light go out

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override { down = false; repaint(); }

    juce::String getTooltip() override;

private:
    const int voice;
    const juce::Colour colour;
    juce::uint32 litUntil = 0;
    bool lit = false, down = false;
};

//
// The whole front panel, at one fixed design size. The editor scales it to whatever size
// the window is, so the layout is written once in panel coordinates.
//
class Panel final : public juce::Component, private juce::Timer
{
public:
    explicit Panel(Mz909Processor&);
    ~Panel() override;

    static constexpr int designWidth = 1198;
    static constexpr int designHeight = 540;

    void paint(juce::Graphics&) override;
    void resized() override;

    PanelLook& getLook() { return look; }

private:
    struct Column
    {
        juce::String title;
        juce::Colour colour;
        std::vector<PanelKnob*> knobs;
        std::vector<juce::Component*> pads;
        juce::Rectangle<int> bounds;
    };

    // The lights, and a control learning or a CC changing - which may come from the audio
    // thread - repainting.
    void timerCallback() override;
    int seenLearning = -1, seenVersion = -1;
    uint32_t seenHits[tr909::Voice::count] {};

    PanelKnob* knob(const char* id, const char* legend, juce::Colour cap);
    Pad* pad(int voice);
    void column(const char* title, juce::Colour colour,
                std::initializer_list<PanelKnob*> knobs, std::initializer_list<juce::Component*> pads);

    Mz909Processor& processor;
    PanelLook look;

    std::vector<std::unique_ptr<PanelKnob>> knobs;
    std::vector<std::unique_ptr<Pad>> pads;
    Pad* padFor[tr909::Voice::count] {};
    std::vector<Column> columns;

    juce::TextButton accentButton { "ACCENT" };
    juce::ComboBox presetBox;
};

class Mz909Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Mz909Editor(Mz909Processor&);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Panel panel;

    // Hover hints. A child of the editor rather than the desktop, as hosts prefer, and
    // declared after the panel so it goes before the look it borrows from it.
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mz909Editor)
};
