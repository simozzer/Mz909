// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#include "Presets.h"

#include "Params.h"

namespace presets
{

const std::vector<Preset>& all()
{
    static const std::vector<Preset> list = {
        { "Init", {} },

        { "House", {
            { "bdDecay", 6 }, { "bdAttack", 6 }, { "bdLevel", 9 }, { "cpLevel", 8 },
            { "ohDecay", 3.5f }, { "chDecay", 2.5f }, { "hhLevel", 7 } } },

        { "Techno", {
            { "bdDecay", 7.5f }, { "bdTune", 3 }, { "bdAttack", 8 }, { "bdLevel", 9 },
            { "sdTune", 6 }, { "sdSnappy", 7 }, { "rsLevel", 7 }, { "ohDecay", 2.5f }, { "accent", 7 } } },

        { "Long Kick", {
            { "bdDecay", 10 }, { "bdTune", 7 }, { "bdAttack", 3 }, { "bdLevel", 9 }, { "volume", 6.5f } } },

        { "Punchy", {
            { "bdDecay", 3.5f }, { "bdTune", 2 }, { "bdAttack", 9 },
            { "sdTune", 7 }, { "sdTone", 7 }, { "sdSnappy", 7 }, { "chDecay", 1.5f }, { "accent", 6 } } },

        { "Deep Toms", {
            { "ltTune", 1 }, { "mtTune", 2 }, { "htTune", 3 },
            { "ltDecay", 8 }, { "mtDecay", 8 }, { "htDecay", 8 },
            { "ltLevel", 8 }, { "mtLevel", 8 }, { "htLevel", 8 } } },

        { "Bright Metal", {
            { "hhLevel", 7 }, { "ohDecay", 7 }, { "chDecay", 4 },
            { "crTune", 7 }, { "crLevel", 7 }, { "rdTune", 6.5f }, { "rdLevel", 7 } } },

        { "Dark Metal", {
            { "ohDecay", 6 }, { "crTune", 2 }, { "rdTune", 3 }, { "sdTone", 3 } } },

        { "Hard Accents", {
            { "accent", 10 }, { "bdDecay", 6 }, { "sdSnappy", 6 } } },
    };

    return list;
}

void apply(const Preset& p, juce::AudioProcessorValueTreeState& state)
{
    for (const auto& spec : params::specs())
        if (auto* param = state.getParameter(spec.id))
            param->setValueNotifyingHost(param->getDefaultValue());

    for (const auto& [id, value] : p.values)
        if (auto* param = state.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1((float) value));
}

} // namespace presets
