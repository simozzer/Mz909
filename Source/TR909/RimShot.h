// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Trigger.h"

namespace tr909
{

//
// RIM SHOT: LEVEL.
//
// The 808's rim shot, carried over and sharpened: the shaped trigger kicks two high-Q
// band passes - resonators, ringing at about 500 Hz and 1.7 kHz - whose sum goes through a
// VCA overdriven on a very short envelope, then a high pass. The overdrive is the crack.
//
// The structure follows the 808's; the 909's pitches, times and filter are reasoned from
// the sound, not measured.
//
class RimShot
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        shaper.prepare(fs);
        low.prepare(fs);
        low.setup(500.0f, (float) pi * 500.0f * 0.010f);
        high.prepare(fs);
        high.setup(1700.0f, (float) pi * 1700.0f * 0.006f);
        env.prepare(fs);
        env.setDecay(0.0045f);
        hp.prepare(fs);
        hp.setup(350.0f, 0.7f);
        reset();
    }

    void trigger(float height) { pulse.fire(height, fs); }

    float process()
    {
        const float p = pulse.next();
        const float s = shaper.process(p);

        const float ring = 0.7f * low.process(s) + high.process(s);
        hp.process(softClip(ring * env.process(p) * overdrive));
        return hp.hp * gain;
    }

    bool quiet() const
    {
        return ! pulse.high() && env.value() < 1.0e-5f && low.energy() + high.energy() + hp.energy() < 1.0e-6f;
    }

    void reset()
    {
        shaper.reset();
        low.reset();
        high.reset();
        env.reset();
        hp.reset();
    }

private:
    static constexpr float overdrive = 4.0f;
    static constexpr float gain = 0.7f;

    float fs = 48000;
    TriggerPulse pulse;
    PulseShaper shaper;
    Svf low, high, hp;
    Envelope env;
};

} // namespace tr909
