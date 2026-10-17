// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace presets
{

// A factory kit: only the knobs that differ from the panel's defaults, in panel units.
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, double>> values;
};

const std::vector<Preset>& all();

// Every parameter to its default, then the preset's own values on top.
void apply(const Preset& p, juce::AudioProcessorValueTreeState& state);

} // namespace presets
