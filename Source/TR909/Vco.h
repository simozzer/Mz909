// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Dsp.h"

namespace tr909
{

//
// The 909's drum oscillator. Where the 808 rings a resonator, the 909's bass drum, snare
// and toms each run a voltage-controlled oscillator, whose triangle a transistor shaper
// rounds off into a near-sine - near, not pure: the corners stay a little pointed, which
// is part of why a 909 kick cuts through where an 808's booms.
//
// Because it is an oscillator rather than a ring, its pitch can be swept as far as the
// envelope likes, and every hit restarts it from the same point in its cycle, so every hit
// starts the same way.
//
// The harmonics of a triangle fall away as 1/n^2, so at drum pitches it needs no
// band-limiting.
//
class DrumVco
{
public:
    void prepare(float sampleRate) { invFs = 1.0f / sampleRate; }

    // from the zero crossing, rising
    void restart() { phase = 0.0f; }

    float process(float hz)
    {
        phase += hz * invFs;
        if (phase >= 1.0f) phase -= std::floor(phase);

        const float tri = phase < 0.25f ? 4.0f * phase
                        : phase < 0.75f ? 2.0f - 4.0f * phase
                                        : 4.0f * phase - 4.0f;
        return softClip(rounding * tri) * norm;
    }

private:
    static constexpr float rounding = 1.7f;
    const float norm = 1.0f / softClip(rounding);

    float invFs = 1.0f / 48000.0f, phase = 0;
};

} // namespace tr909
