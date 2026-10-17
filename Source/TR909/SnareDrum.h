// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Trigger.h"
#include "Vco.h"

namespace tr909
{

//
// SNARE DRUM: LEVEL, TUNE, TONE, SNAPPY.
//
//  - The drum head is two VCOs, a little under an octave apart, each through its own VCA;
//    the upper one dies sooner. Both start a little sharp and drop as the head settles.
//    TUNE moves them together.
//  - The snares are white noise, low-passed and high-passed, through a VCA on an envelope
//    longer than the head's - the 909 snare's hiss outlasts its body. SNAPPY is how much
//    of it goes in, and TONE how bright it is.
//
// The structure is the circuit's. The pitches, times and filters are reasoned from the
// sound, not measured.
//
class SnareDrum
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        low.prepare(fs);
        high.prepare(fs);
        lowAmp.prepare(fs);
        lowAmp.setDecay(0.05f);
        highAmp.prepare(fs);
        highAmp.setDecay(0.028f);
        pitchFall = decayCoef(0.012f, fs);
        noiseHp.prepare(fs);
        noiseHp.setup(500.0f, 0.7f);
        noiseAmp.prepare(fs);
        noiseAmp.setDecay(0.075f);
        set(5.0f, 5.0f, 5.0f);
        reset();
    }

    void set(float tunePanel, float tonePanel, float snappyPanel)
    {
        ratio = expLaw(tunePanel / 10.0f, 0.8f, 1.25f);
        noiseLpCoef = onePoleCoef(tauOf(expLaw(tonePanel / 10.0f, 2500.0f, 11000.0f)), fs);
        snappy = clamp(snappyPanel / 10.0f, 0.0f, 1.0f);
    }

    void trigger(float height)
    {
        pulse.fire(height, fs);
        pitch = 1.0f;
        low.restart();
        high.restart();
    }

    float process(Random& rng)
    {
        const float p = pulse.next();

        pitch *= pitchFall;
        const float bend = ratio * (1.0f + pitchDrop * pitch);
        const float head = low.process(lowHz * bend) * lowAmp.process(p)
                         + highMix * high.process(highHz * bend) * highAmp.process(p);

        const float hiss = noiseLp2.lowPass(noiseLp1.lowPass(rng.white(), noiseLpCoef), noiseLpCoef);
        noiseHp.process(hiss);
        const float snares = noiseHp.hp * noiseAmp.process(p);

        return softClip(headGain * head + snappy * snareGain * snares) * gain;
    }

    bool quiet() const
    {
        return ! pulse.high() && lowAmp.value() + highAmp.value() + noiseAmp.value() < 1.0e-5f;
    }

    void reset()
    {
        lowAmp.reset();
        highAmp.reset();
        noiseAmp.reset();
        noiseLp1.reset();
        noiseLp2.reset();
        noiseHp.reset();
        pitch = 0.0f;
    }

private:
    static constexpr float lowHz = 185.0f;
    static constexpr float highHz = 330.0f;
    static constexpr float highMix = 0.6f;
    static constexpr float pitchDrop = 0.5f;
    static constexpr float headGain = 0.45f;
    static constexpr float snareGain = 0.8f;
    static constexpr float gain = 1.6f;

    float fs = 48000;
    TriggerPulse pulse;
    DrumVco low, high;
    Envelope lowAmp, highAmp, noiseAmp;
    OnePole noiseLp1, noiseLp2;
    Svf noiseHp;

    float ratio = 1, noiseLpCoef = 1, snappy = 0.5f;
    float pitch = 0, pitchFall = 0;
};

} // namespace tr909
