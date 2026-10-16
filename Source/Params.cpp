// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#include "Params.h"

namespace params
{

using S = tr909::Settings;

const std::vector<Spec>& specs()
{
    //
    // CCs use the numbers MIDI leaves undefined, 14-31 and 102-119, in panel order, left
    // to right and top to bottom - so a controller's row of knobs lands on them in order.
    // VOLUME is General MIDI's, 7.
    //
    static const std::vector<Spec> table = {
        { "accent",   "Accent",            14, "How much harder an accented hit strikes: velocity above 100 is an accent. It does not just turn the sound up - an accented kick sweeps from higher, and the drums push their output stages harder.",
          &S::accent },

        { "bdLevel",  "Bass Drum Level",   15, "Level of the bass drum.",                                                            &S::bdLevel },
        { "bdTune",   "Bass Drum Tune",    16, "How far and how long the kick's pitch falls: a short, tight drop at 0, a long, deep sweep at 10.", &S::bdTune },
        { "bdAttack", "Bass Drum Attack",  17, "The beater: how much click and noise is on the front of the kick.",                   &S::bdAttack },
        { "bdDecay",  "Bass Drum Decay",   18, "How long the bass drum rings: a short thump at 0, a long boom at 10.",               &S::bdDecay },

        { "sdLevel",  "Snare Drum Level",  19, "Level of the snare drum.",                                                           &S::sdLevel },
        { "sdTune",   "Snare Drum Tune",   20, "Pitch of the drum head, a little over a quarter-octave either way.",                 &S::sdTune },
        { "sdTone",   "Snare Drum Tone",   21, "How bright the snares are: dark at 0, crisp at 10.",                                 &S::sdTone },
        { "sdSnappy", "Snare Drum Snappy", 22, "How much of the snares - the noise - goes in.",                                      &S::sdSnappy },

        { "ltLevel",  "Low Tom Level",     23, "Level of the low tom.",                                                              &S::ltLevel },
        { "ltTune",   "Low Tom Tune",      24, "Pitch of the low tom, half an octave either way.",                                   &S::ltTune },
        { "ltDecay",  "Low Tom Decay",     25, "How long the low tom rings.",                                                        &S::ltDecay },
        { "mtLevel",  "Mid Tom Level",     26, "Level of the mid tom.",                                                              &S::mtLevel },
        { "mtTune",   "Mid Tom Tune",      27, "Pitch of the mid tom, half an octave either way.",                                   &S::mtTune },
        { "mtDecay",  "Mid Tom Decay",     28, "How long the mid tom rings.",                                                        &S::mtDecay },
        { "htLevel",  "Hi Tom Level",      29, "Level of the hi tom.",                                                               &S::htLevel },
        { "htTune",   "Hi Tom Tune",       30, "Pitch of the hi tom, half an octave either way.",                                    &S::htTune },
        { "htDecay",  "Hi Tom Decay",      31, "How long the hi tom rings.",                                                         &S::htDecay },

        { "rsLevel",  "Rim Shot Level",   102, "Level of the rim shot.",                                                             &S::rsLevel },
        { "cpLevel",  "Hand Clap Level",  103, "Level of the hand clap.",                                                            &S::cpLevel },

        { "hhLevel",  "Hihat Level",      104, "Level of both hihats, which share one DAC and one VCA.",                         &S::hhLevel },
        { "chDecay",  "Closed Hihat Decay", 105, "How long the closed hihat rings before its VCA shuts. 5 is the measured hat.", &S::chDecay },
        { "ohDecay",  "Open Hihat Decay", 106, "How long the open hihat rings before its VCA shuts; 10 is the measured hat. A closed hihat cuts it off.", &S::ohDecay },

        { "crLevel",  "Crash Level",      107, "Level of the crash cymbal.",                                                         &S::crLevel },
        { "crTune",   "Crash Tune",       108, "The speed the crash's sample plays at: lower and longer at 0, higher and shorter at 10.", &S::crTune },
        { "rdLevel",  "Ride Level",       109, "Level of the ride cymbal.",                                                          &S::rdLevel },
        { "rdTune",   "Ride Tune",        110, "The speed the ride's sample plays at: lower and longer at 0, higher and shorter at 10.", &S::rdTune },

        { "volume",   "Volume",             7, "Master output level.",                                                               &S::volume },
    };

    return table;
}

int indexOf(const juce::String& id)
{
    const auto& t = specs();
    for (size_t i = 0; i < t.size(); ++i)
        if (id == t[i].id) return (int) i;
    return -1;
}

juce::String hintFor(int index, int cc, bool learning)
{
    if (index < 0 || index >= (int) specs().size()) return {};
    const auto& p = specs()[(size_t) index];

    juce::String midi;
    if (learning)          midi = "MIDI LEARN: move a knob on your controller. Right-click to cancel.";
    else if (cc < 0)       midi = "No MIDI CC (default " + juce::String(p.cc) + "). Right-click to learn one.";
    else if (cc != p.cc)   midi = "MIDI CC " + juce::String(cc) + ", learned (default " + juce::String(p.cc) + "). Right-click to change.";
    else                   midi = "MIDI CC " + juce::String(cc) + ". Right-click to learn another.";

    return juce::String(p.name).toUpperCase() + "\n" + p.hint + "\n" + midi;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const S defaults;

    for (const auto& p : specs())
    {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { p.id, 1 }, p.name, juce::NormalisableRange<float> (0.0f, 10.0f, 0.0f), defaults.*(p.f),
            juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [] (float v, int) { return juce::String (v, 1); })));
    }

    return layout;
}

Reader::Reader(juce::AudioProcessorValueTreeState& state)
{
    for (const auto& p : specs())
    {
        auto* v = state.getRawParameterValue(p.id);
        jassert(v != nullptr);
        values.push_back(v);
    }
}

tr909::Settings Reader::read() const
{
    S s;
    const auto& table = specs();

    for (size_t n = 0; n < table.size(); ++n)
        s.*(table[n].f) = values[n]->load(std::memory_order_relaxed);

    return s;
}

} // namespace params
