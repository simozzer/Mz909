// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Trigger.h"
#include "Vco.h"

namespace tr909
{

enum class TomSize { Low, Mid, High };

//
// LOW, MID and HI TOM: LEVEL, TUNE, DECAY each.
//
// Built like the bass drum, smaller: a drum VCO, restarted by each hit, with a pitch that
// starts high and falls - the 909 tom's "boww" - through a VCA whose envelope DECAY sets,
// and a short puff of noise for the stick. Nominal pitches are about 100, 140 and 200 Hz.
// TUNE moves the whole tom, sweep and all, half an octave either way.
//
// The structure is the circuit's. The pitches, the sweep and the times are reasoned from
// the sound, not measured.
//
class Tom
{
public:
    explicit Tom(TomSize s)
        : base(s == TomSize::Low ? 100.0f : s == TomSize::Mid ? 140.0f : 200.0f),
          length(s == TomSize::Low ? 1.2f : s == TomSize::Mid ? 1.0f : 0.85f)
    {
    }

    void prepare(float sampleRate)
    {
        fs = sampleRate;
        vco.prepare(fs);
        amp.prepare(fs);
        pitchFall = decayCoef(0.03f, fs);
        noiseLpCoef = onePoleCoef(tauOf(1800.0f), fs);
        noiseFall = decayCoef(0.012f, fs);
        set(5.0f, 5.0f);
        reset();
    }

    void set(float tunePanel, float decayPanel)
    {
        hz = base * expLaw(tunePanel / 10.0f, 0.70710678f, 1.41421356f);
        amp.setDecay(length * expLaw(decayPanel / 10.0f, 0.06f, 0.35f));
    }

    void trigger(float height)
    {
        pulse.fire(height, fs);
        pitch = height;
        noise = height;
        vco.restart();
    }

    float process(Random& rng)
    {
        const float p = pulse.next();

        pitch *= pitchFall;
        const float body = vco.process(hz * (1.0f + sweep * pitch)) * amp.process(p);

        noise *= noiseFall;
        const float stick = noiseLp.lowPass(rng.white(), noiseLpCoef) * noise;

        return softClip(drive * (body + stickLevel * stick)) * gain;
    }

    bool quiet() const { return ! pulse.high() && amp.value() < 1.0e-5f && noise < 1.0e-5f; }

    void reset()
    {
        amp.reset();
        noiseLp.reset();
        pitch = noise = 0.0f;
    }

private:
    static constexpr float sweep = 0.6f;
    static constexpr float stickLevel = 0.2f;
    static constexpr float drive = 0.9f;
    static constexpr float gain = 1.2f;

    const float base, length;

    float fs = 48000;
    TriggerPulse pulse;
    DrumVco vco;
    Envelope amp;
    OnePole noiseLp;

    float hz = 100, pitchFall = 0, noiseLpCoef = 1, noiseFall = 0;
    float pitch = 0, noise = 0;
};

} // namespace tr909
