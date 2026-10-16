// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "TR909/Settings.h"

#include <vector>

namespace params
{

//
// One row per front-panel knob: its parameter ID, the name a host shows, and the field of
// tr909::Settings it drives. The layout, the reader and the presets all work from this, so
// a control cannot exist in one and be missing from another.
//
// IDs are saved in every host project. Never rename one.
//
struct Spec
{
    const char* id;
    const char* name;
    int cc;                                  // the MIDI CC that moves it
    const char* hint;                        // what it does, for the hover hint
    float tr909::Settings::* f;              // 0..10, as printed round the knob
};

const std::vector<Spec>& specs();
int indexOf(const juce::String& id);   // -1 if none

// The hover hint: name, what it does, and the CC it answers to now - which MIDI learn may
// have changed from the default in the table (cc < 0 for none).
juce::String hintFor(int index, int cc, bool learning);

// CCs that can be learned: not bank select (0, 32), not the channel mode messages (120 up).
inline bool learnable(int cc) { return cc >= 1 && cc < 120 && cc != 32; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Reads every parameter into Settings, from the audio thread, without a lookup by name.
class Reader
{
public:
    explicit Reader(juce::AudioProcessorValueTreeState& state);
    tr909::Settings read() const;

private:
    std::vector<std::atomic<float>*> values;
};

} // namespace params
