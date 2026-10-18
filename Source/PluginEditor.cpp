// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#include "PluginEditor.h"

#include "Params.h"
#include "Presets.h"

namespace colours
{
    const juce::Colour panel    { 0xff1b1b1d };
    const juce::Colour section  { 0xff252528 };
    const juce::Colour edge     { 0xff3a3a40 };
    const juce::Colour legend   { 0xffe8e6e0 };
    const juce::Colour dim      { 0xff8e8c88 };
    const juce::Colour skirt    { 0xff0b0b0c };

    // the 909's band, from red through orange to amber, and its silver keys
    const juce::Colour red      { 0xffd8392b };
    const juce::Colour orange   { 0xffef6c1f };
    const juce::Colour amber    { 0xfff2a23a };
    const juce::Colour silver   { 0xffd9d8d4 };
    const juce::Colour grey     { 0xffb4b2ad };

    // the drums orange, rim shot and clap amber, the samples silver
    juce::Colour forPad(int voice)
    {
        using namespace tr909;
        if (voice <= Voice::HT) return orange;
        if (voice <= Voice::CP) return amber;
        return silver;
    }
}

//============================================================================== PanelLook

PanelLook::PanelLook()
{
    setColour(juce::ComboBox::backgroundColourId, colours::section);
    setColour(juce::ComboBox::outlineColourId, colours::edge);
    setColour(juce::ComboBox::textColourId, colours::legend);
    setColour(juce::ComboBox::arrowColourId, colours::legend);
    setColour(juce::PopupMenu::backgroundColourId, colours::section);
    setColour(juce::PopupMenu::textColourId, colours::legend);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::red);
    setColour(juce::TextButton::textColourOffId, colours::legend);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::BubbleComponent::backgroundColourId, colours::section);
    setColour(juce::BubbleComponent::outlineColourId, colours::edge);
    setColour(juce::TooltipWindow::textColourId, colours::legend);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff111214));
    setColour(juce::TooltipWindow::outlineColourId, colours::red);
}

void PanelLook::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                 float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto colour = juce::Colour((juce::uint32) (juce::int64) slider.getProperties()["cap"]);
    const auto area = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
    const auto c = area.getCentre();
    const float r = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 7.0f;

    // the scale, 0 to 10, round the skirt
    g.setColour(colours::dim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const bool major = i % 5 == 0;
        const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
        g.drawLine(juce::Line<float>(c + dir * (r + 2.0f), c + dir * (r + (major ? 7.0f : 5.0f))), major ? 1.4f : 1.0f);
    }

    // where it is set, as an arc in the section's colour
    const float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, r + 0.5f, r + 0.5f, 0.0f, startAngle, angle, true);
    g.setColour(colour.withAlpha(0.85f));
    g.strokePath(arc, juce::PathStrokeType(2.0f));

    // the skirt, and the cap on it
    g.setColour(colours::skirt);
    g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));

    const auto cap = juce::Rectangle<float>(r * 1.45f, r * 1.45f).withCentre(c);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4a50), cap.getX(), cap.getY(),
                                           juce::Colour(0xff1e1e22), cap.getX(), cap.getBottom(), false));
    g.fillEllipse(cap);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(cap, 1.0f);

    // the pointer
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(colour);
    g.drawLine(juce::Line<float>(c + dir * (r * 0.25f), c + dir * (r * 0.95f)), 2.5f);
}

void PanelLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                     bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(down ? juce::Colour(0xff0c0c0c) : juce::Colour(0xff161616));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(highlighted ? colours::dim : colours::edge);
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    // the LED
    const auto led = juce::Rectangle<float>(6.0f, 6.0f).withCentre({ r.getCentreX(), r.getY() + 7.0f });
    g.setColour(b.getToggleState() ? colours::red.brighter(0.4f) : juce::Colour(0xff3a1714));
    g.fillEllipse(led);
}

//============================================================================== MIDI learn

namespace midiLearn
{

juce::String hint(const Mz909Processor& p, int index)
{
    return params::hintFor(index, p.ccFor(index), p.learningIndex() == index);
}

void showMenu(Mz909Processor& p, int index)
{
    const int cc = p.ccFor(index);
    const int def = params::specs()[(size_t) index].cc;
    const bool learning = p.learningIndex() == index;

    juce::PopupMenu m;
    m.addSectionHeader(juce::String(params::specs()[(size_t) index].name) +
                       (cc >= 0 ? "  -  CC " + juce::String(cc) : "  -  no CC"));

    if (learning) m.addItem("Cancel MIDI Learn", [&p] { p.learn(-1); });
    else          m.addItem("MIDI Learn", [&p, index] { p.learn(index); });

    m.addItem("Forget CC", cc >= 0, false, [&p, index] { p.forgetCc(index); });
    m.addItem("Restore default (CC " + juce::String(def) + ")", cc != def, false,
              [&p, index] { p.restoreDefaultCc(index); });
    m.addSeparator();
    m.addItem("Reset all CCs to defaults", [&p] { p.resetAllCcs(); });

    m.showMenuAsync(juce::PopupMenu::Options());
}

// While a control waits for its CC: a red outline and LEARN.
void drawLearning(juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour(colours::red);
    g.drawRoundedRectangle(r.reduced(1.0f), 4.0f, 2.0f);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("LEARN", r.removeFromTop(12.0f), juce::Justification::centred);
}

} // namespace midiLearn

//============================================================================== PanelKnob

PanelKnob::PanelKnob(Mz909Processor& p, const juce::String& paramId, const juce::String& legendText, juce::Colour cap)
    : processor(p), index(params::indexOf(paramId)), legend(legendText)
{
    jassert(index >= 0);
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters(juce::degreesToRadians(-135.0f), juce::degreesToRadians(135.0f), true);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity(180);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    slider.getProperties().set("cap", (juce::int64) cap.getARGB());
    addAndMakeVisible(slider);

    // the same hint and menu over the knob as over its legend
    slider.hint = [this] { return getTooltip(); };
    slider.onRightClick = [this] { midiLearn::showMenu(processor, index); };

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, paramId, slider);

    auto* param = p.state.getParameter(paramId);
    slider.setDoubleClickReturnValue(true, (double) param->convertFrom0to1(param->getDefaultValue()));
}

void PanelKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom(18);
    slider.setBounds(r.withSizeKeepingCentre(juce::jmin(r.getWidth(), 66), r.getHeight()));
}

void PanelKnob::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) midiLearn::showMenu(processor, index);
}

void PanelKnob::paint(juce::Graphics& g)
{
    if (processor.learningIndex() == index)
        midiLearn::drawLearning(g, getLocalBounds().toFloat());

    g.setColour(colours::legend);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText(legend, getLocalBounds().removeFromBottom(18), juce::Justification::centredTop);
}

//============================================================================== Pad

Pad::Pad(int v, juce::Colour c) : voice(v), colour(c)
{
}

void Pad::tick()
{
    const bool now = juce::Time::getMillisecondCounter() < litUntil;
    if (now != lit)
    {
        lit = now;
        repaint();
    }
}

void Pad::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    down = true;
    if (onStrike) onStrike(e.mods.isShiftDown());
    repaint();
}

juce::String Pad::getTooltip()
{
    const auto& info = tr909::voiceInfo(voice);
    return juce::String(info.name).toUpperCase() + "\nMIDI note " + juce::String(info.note) +
           " (" + juce::MidiMessage::getMidiNoteName(info.note, true, true, 3) + ")."
           "\nClick to play it; shift-click, or light ACCENT, to play it accented.";
}

void Pad::paint(juce::Graphics& g)
{
    const auto& info = tr909::voiceInfo(voice);
    auto r = getLocalBounds().toFloat();

    // the LED above the key
    const auto led = juce::Rectangle<float>(7.0f, 7.0f).withCentre({ r.getCentreX(), r.getY() + 5.0f });
    g.setColour(lit ? colours::red.brighter(0.5f) : juce::Colour(0xff3a1714));
    g.fillEllipse(led);
    if (lit)
    {
        g.setColour(colours::red.withAlpha(0.35f));
        g.fillEllipse(led.expanded(3.0f));
    }

    // the key
    auto key = r.withTrimmedTop(14.0f).reduced(down ? 2.0f : 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(key.translated(0.0f, 2.0f), 3.0f);
    g.setColour(lit ? colour.brighter(0.25f) : colour.darker(0.15f));
    g.fillRoundedRectangle(key, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(lit ? 0.35f : 0.15f));
    g.fillRoundedRectangle(key.withHeight(4.0f).reduced(2.0f, 0.0f).translated(0.0f, 2.0f), 2.0f);

    g.setColour(juce::Colour(0xff1b1b1d));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(info.code, key.withTrimmedTop(8.0f).withHeight(18.0f), juce::Justification::centred);
    g.setFont(juce::FontOptions(9.5f));
    g.drawText(juce::String(info.note), key.withTrimmedBottom(5.0f).removeFromBottom(14.0f), juce::Justification::centred);
}

//============================================================================== Panel

Panel::Panel(Mz909Processor& p) : processor(p)
{
    setLookAndFeel(&look);
    startTimerHz(30);

    using namespace tr909;
    using colours::red;   using colours::orange;
    using colours::amber; using colours::silver; using colours::grey;

    column("ACCENT", red,                 { knob("accent", "LEVEL", red) }, { &accentButton });

    column("BASS DRUM", orange,           { knob("bdLevel", "LEVEL", orange), knob("bdTune", "TUNE", orange),
                                            knob("bdAttack", "ATTACK", orange), knob("bdDecay", "DECAY", orange) },
                                          { pad(Voice::BD) });
    column("SNARE DRUM", orange,          { knob("sdLevel", "LEVEL", orange), knob("sdTune", "TUNE", orange),
                                            knob("sdTone", "TONE", orange), knob("sdSnappy", "SNAPPY", orange) },
                                          { pad(Voice::SD) });
    column("LOW TOM", orange,             { knob("ltLevel", "LEVEL", orange), knob("ltTune", "TUNE", orange),
                                            knob("ltDecay", "DECAY", orange) }, { pad(Voice::LT) });
    column("MID TOM", orange,             { knob("mtLevel", "LEVEL", orange), knob("mtTune", "TUNE", orange),
                                            knob("mtDecay", "DECAY", orange) }, { pad(Voice::MT) });
    column("HI TOM", orange,              { knob("htLevel", "LEVEL", orange), knob("htTune", "TUNE", orange),
                                            knob("htDecay", "DECAY", orange) }, { pad(Voice::HT) });
    column("RIM SHOT", amber,             { knob("rsLevel", "LEVEL", amber) }, { pad(Voice::RS) });
    column("HAND CLAP", amber,            { knob("cpLevel", "LEVEL", amber) }, { pad(Voice::CP) });
    column("HI HAT", silver,              { knob("hhLevel", "LEVEL", silver), knob("chDecay", "CH DECAY", silver),
                                            knob("ohDecay", "OH DECAY", silver) }, { pad(Voice::CH), pad(Voice::OH) });
    column("CRASH", silver,               { knob("crLevel", "LEVEL", silver), knob("crTune", "TUNE", silver) },
                                          { pad(Voice::CC) });
    column("RIDE", silver,                { knob("rdLevel", "LEVEL", silver), knob("rdTune", "TUNE", silver) },
                                          { pad(Voice::RC) });
    column("MASTER", grey,                { knob("volume", "VOLUME", grey) }, {});

    accentButton.setClickingTogglesState(true);
    accentButton.setTooltip("ACCENT\nLit, the pads play accented hits - as velocity above 100 does from MIDI. "
                            "Shift-click a pad for one accented hit.");
    addAndMakeVisible(accentButton);

    presetBox.setTooltip("Factory kits. Your own edits are saved with the project.");
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem(processor.getProgramName(i), i + 1);
    presetBox.setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        processor.setCurrentProgram(presetBox.getSelectedId() - 1);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withProgramChanged(true));
    };
    addAndMakeVisible(presetBox);

    //
    // Where to come back to: the version this is, and the page the next one will be on.
    // Nothing here checks for updates by itself - the plugin never goes online unasked.
    // It takes no keyboard focus, or a host's space bar could open it rather than play.
    //
    projectLink.setButtonText("Mz909 " JucePlugin_VersionString "  -  github.com/simozzer/Mz909");
    projectLink.setURL(juce::URL("https://github.com/simozzer/Mz909"));
    projectLink.setFont(juce::FontOptions(12.0f), false, juce::Justification::centredRight);
    projectLink.setColour(juce::HyperlinkButton::textColourId, colours::dim);
    projectLink.setTooltip("The project's page: the latest version and what's new in it, the source, "
                           "and where to report a problem. Opens in your browser.");
    projectLink.setWantsKeyboardFocus(false);
    addAndMakeVisible(projectLink);

    // start the lights from where the engine is, so opening the editor does not flash them
    for (int v = 0; v < Voice::count; ++v)
        seenHits[v] = processor.engineView().hitCount(v);
}

Panel::~Panel()
{
    setLookAndFeel(nullptr);
}

PanelKnob* Panel::knob(const char* id, const char* legend, juce::Colour cap)
{
    knobs.push_back(std::make_unique<PanelKnob>(processor, id, legend, cap));
    addAndMakeVisible(*knobs.back());
    return knobs.back().get();
}

Pad* Panel::pad(int voice)
{
    pads.push_back(std::make_unique<Pad>(voice, colours::forPad(voice)));
    auto* p = pads.back().get();
    p->onStrike = [this, voice] (bool shift) { processor.strike(voice, shift || accentButton.getToggleState()); };
    padFor[voice] = p;
    addAndMakeVisible(*p);
    return p;
}

void Panel::column(const char* title, juce::Colour colour,
                   std::initializer_list<PanelKnob*> k, std::initializer_list<juce::Component*> p)
{
    columns.push_back({ title, colour, k, p, {} });
}

void Panel::timerCallback()
{
    for (int v = 0; v < tr909::Voice::count; ++v)
    {
        const uint32_t h = processor.engineView().hitCount(v);
        if (h != seenHits[v])
        {
            seenHits[v] = h;
            if (padFor[v] != nullptr) padFor[v]->light();
        }
    }
    for (auto& p : pads) p->tick();

    const int l = processor.learningIndex(), ver = processor.midiMapVersion();
    if (l == seenLearning && ver == seenVersion) return;

    seenLearning = l;
    seenVersion = ver;
    for (auto& k : knobs) k->repaint();
}

void Panel::resized()
{
    constexpr int margin = 14, width = 92, gap = 6, top = 62, height = 438;
    constexpr int title = 44, knobHeight = 76, padTop = 358, padWidth = 38, padHeight = 70;

    presetBox.setBounds(designWidth - margin - 220, 14, 220, 26);
    projectLink.setBounds(presetBox.getX() - 16 - 420, presetBox.getY(), 420, presetBox.getHeight());

    int x = margin;
    for (auto& c : columns)
    {
        c.bounds = { x, top, width, height };

        int y = top + title;
        for (auto* k : c.knobs)
        {
            k->setBounds(x + 4, y, width - 8, knobHeight);
            y += knobHeight;
        }

        if (c.pads.size() == 1 && dynamic_cast<juce::Button*>(c.pads[0]) != nullptr)
            c.pads[0]->setBounds(x + 10, top + padTop + 22, width - 20, 40);
        else
        {
            const int n = (int) c.pads.size();
            const int span = n * padWidth + (n - 1) * 6;
            int px = x + (width - span) / 2;
            for (auto* p : c.pads)
            {
                p->setBounds(px, top + padTop, padWidth, padHeight);
                px += padWidth + 6;
            }
        }

        x += width + gap;
    }
}

void Panel::paint(juce::Graphics& g)
{
    g.fillAll(colours::panel);

    // the 909's band of colour along the top, shading from red to amber
    juce::ColourGradient band(colours::red, 0.0f, 0.0f, colours::amber, (float) designWidth, 0.0f, false);
    band.addColour(0.5, colours::orange);
    g.setGradientFill(band);
    g.fillRect(0, 0, designWidth, 4);

    g.setColour(colours::legend);
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText("Mz909", 16, 10, 120, 38, juce::Justification::centredLeft);
    g.setColour(colours::orange);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("RHYTHM COMPOSER", 114, 10, 300, 40, juce::Justification::centredLeft);

    for (const auto& c : columns)
    {
        const auto r = c.bounds.toFloat();
        g.setColour(colours::section);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(colours::edge);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);

        g.setColour(colours::legend);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawFittedText(c.title, c.bounds.withHeight(38).reduced(4, 2), juce::Justification::centred, 2);
        g.setColour(c.colour);
        g.fillRect(c.bounds.getX() + 8, c.bounds.getY() + 39, c.bounds.getWidth() - 16, 2);
    }

    g.setColour(colours::dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Plays from MIDI on the General MIDI drum map - the number on each pad is its note. "
               "Velocity above 100 is an accent. Right-click a knob to MIDI-learn it.",
               14, designHeight - 36, designWidth - 28, 24, juce::Justification::centredLeft);

    // from CMakeLists.txt, with the (C) the version resource needs made a proper sign
    g.drawText(juce::String(MZ909_COPYRIGHT),
               14, designHeight - 36, designWidth - 28, 24, juce::Justification::centredRight);
}

//============================================================================== Mz909Editor

Mz909Editor::Mz909Editor(Mz909Processor& p)
    : AudioProcessorEditor(&p), panel(p)
{
    tooltips.setLookAndFeel(&panel.getLook());
    addAndMakeVisible(panel);
    panel.setBounds(0, 0, Panel::designWidth, Panel::designHeight);

    setResizable(true, true);
    setResizeLimits(Panel::designWidth / 2, Panel::designHeight / 2,
                    Panel::designWidth * 2, Panel::designHeight * 2);
    getConstrainer()->setFixedAspectRatio((double) Panel::designWidth / Panel::designHeight);
    setSize(Panel::designWidth, Panel::designHeight);
}

void Mz909Editor::paint(juce::Graphics& g)
{
    g.fillAll(colours::panel);
}

void Mz909Editor::resized()
{
    const float scale = (float) getWidth() / (float) Panel::designWidth;
    panel.setTransform(juce::AffineTransform::scale(scale));
}
