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

//
// BASS DRUM: LEVEL, TUNE, ATTACK, DECAY.
//
//  - The body is the drum VCO, restarted by each hit, through a VCA whose envelope DECAY
//    sets.
//  - A pitch envelope, charged by the trigger, starts the VCO several times higher and lets
//    it fall exponentially to the body's pitch, about 50 Hz. That fall is the 909 kick's
//    "dooo". TUNE sets how far and how long the pitch falls: a short, tight drop at 0, a
//    long, deep sweep at 10. The body's own pitch moves little.
//  - ATTACK is the beater: the trigger's edge, differentiated into a click, and a burst
//    of low-passed noise, mixed in at the level ATTACK sets.
//  - The output stage is driven into soft saturation. An accent's taller trigger charges
//    the pitch envelope higher - a deeper sweep - and pushes the VCA harder, so an accented
//    kick punches rather than just getting louder.
//
// The structure is the circuit's. The pitches, times and levels are reasoned from the sound
// and from the circuit, not measured from a unit.
//
class BassDrum
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        vco.prepare(fs);
        amp.prepare(fs);
        edgeCoef = onePoleCoef(0.00025f, fs);
        noiseLpCoef = onePoleCoef(tauOf(4500.0f), fs);
        noiseFall = decayCoef(0.003f, fs);
        set(5.0f, 5.0f, 5.0f);
        reset();
    }

    void set(float tunePanel, float attackPanel, float decayPanel)
    {
        const float t = tunePanel / 10.0f;
        sweepDepth = expLaw(t, 1.5f, 5.0f);
        pitchFall = decayCoef(expLaw(t, 0.008f, 0.045f), fs);
        hz = base * (0.97f + 0.06f * clamp(t, 0.0f, 1.0f));
        click = clamp(attackPanel / 10.0f, 0.0f, 1.0f);
        amp.setDecay(expLaw(decayPanel / 10.0f, 0.06f, 0.42f));
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
        const float body = vco.process(hz * (1.0f + sweepDepth * pitch)) * amp.process(p);

        // the beater: the pulse's edges, and a puff of noise
        const float edge = edgeHp.highPass(p, edgeCoef);
        noise *= noiseFall;
        const float puff = noiseLp.lowPass(rng.white(), noiseLpCoef) * noise;
        const float beater = click * (clickEdge * edge + clickNoise * puff);

        return softClip(drive * (body + beater)) * gain;
    }

    bool quiet() const
    {
        return ! pulse.high() && amp.value() < 1.0e-5f && noise < 1.0e-5f && std::fabs(edgeHp.z) < 1.0e-5f;
    }

    void reset()
    {
        amp.reset();
        edgeHp.reset();
        noiseLp.reset();
        pitch = noise = 0.0f;
    }

private:
    static constexpr float base = 50.0f;
    static constexpr float clickEdge = 0.9f;
    static constexpr float clickNoise = 0.6f;
    static constexpr float drive = 0.8f;
    static constexpr float gain = 1.35f;

    float fs = 48000;
    TriggerPulse pulse;
    DrumVco vco;
    Envelope amp;
    OnePole edgeHp, noiseLp;

    float sweepDepth = 3, pitchFall = 0, hz = base, click = 0.5f;
    float edgeCoef = 1, noiseLpCoef = 1, noiseFall = 0;
    float pitch = 0, noise = 0;
};

} // namespace tr909
