// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Trigger.h"

namespace tr909
{

//
// HAND CLAP: LEVEL.
//
// White noise through a band pass a little above 1 kHz, into a VCA driven by two envelopes
// at once. A sawtooth generator gives a burst of quick decays a few milliseconds apart -
// several hands, not quite together - and a slower envelope gives the tail after them, the
// "reverb". The 909's is brighter and its tail longer than the 808's.
//
// The structure is the circuit's; the number of claps, their spacing, the filter and the
// times are reasoned from the sound, not measured.
//
class HandClap
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        bp.prepare(fs);
        bp.setup(1150.0f, 1.8f);
        toothFall = decayCoef(0.0025f, fs);
        tailRise = decayCoef(0.007f, fs);
        tailFall = decayCoef(0.11f, fs);
        spacing = (int) std::lround(0.009f * fs);
        reset();
    }

    void trigger(float height)
    {
        h = height;
        tooth = height;
        teethLeft = claps - 1;
        timer = spacing;
        fast = slow = height;
    }

    float process(Random& rng)
    {
        tooth *= toothFall;
        if (teethLeft > 0 && --timer <= 0)
        {
            tooth = h;
            --teethLeft;
            timer = spacing;
        }

        fast *= tailRise;
        slow *= tailFall;
        const float tail = (slow - fast) * tailLevel;

        bp.process(rng.white());
        return softClip(bp.bandNorm() * (tooth + tail) * drive) * gain;
    }

    bool quiet() const { return teethLeft == 0 && tooth < 1.0e-5f && slow < 1.0e-5f; }

    void reset()
    {
        bp.reset();
        tooth = fast = slow = 0.0f;
        teethLeft = 0;
    }

private:
    static constexpr int claps = 4;           // three quick, and the one the tail grows from
    static constexpr float tailLevel = 0.8f;
    static constexpr float drive = 1.1f;
    static constexpr float gain = 1.25f;

    float fs = 48000;
    Svf bp;

    float h = 1;
    float tooth = 0, toothFall = 0;
    int teethLeft = 0, timer = 0, spacing = 400;
    float fast = 0, slow = 0, tailRise = 0, tailFall = 0;
};

} // namespace tr909
