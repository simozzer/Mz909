// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Dsp.h"

namespace tr909
{

//
// How a sound starts on the 909. As on the 808 before it, the CPU strikes each instrument
// with a short trigger pulse, and an accented step is a taller pulse: the panel's ACCENT
// knob sets how much taller. Every voice makes its envelopes - and the analog ones their
// pitch sweeps - from that pulse, so an accent changes how a sound is struck, not just a
// gain at the output.
//
// Heights are in units of the unaccented trigger: an accent is 1x to 2x by the ACCENT knob.
// That range is reasoned from the sound, not measured.
//
inline float triggerHeight(bool accented, float accentPanel)
{
    return accented ? 1.0f + clamp(accentPanel, 0.0f, 10.0f) / 10.0f : 1.0f;
}

// The sampled voices' VCA, after the DAC, answers an accent harder than the drums' output
// stages do: at full ACCENT an accented hit is 10 dB up, as measured on a 909-style machine.
inline float sampleAccent(float height) { return std::pow(height, 1.661f); }

class TriggerPulse
{
public:
    static constexpr float width = 0.001f;   // seconds

    void fire(float h, float sampleRate)
    {
        height = h;
        remaining = (int) std::lround(width * sampleRate);
    }

    float next()
    {
        if (remaining <= 0) return 0.0f;
        --remaining;
        return height;
    }

    bool high() const { return remaining > 0; }

private:
    float height = 0;
    int remaining = 0;
};

//
// A trigger-driven envelope generator: a capacitor charged through a diode by the pulse
// while it is high, then discharged through a resistor. It rises over the pulse's
// millisecond, falls away as an RC curve, and a new hit charges it from wherever it had
// fallen to rather than from zero.
//
class Envelope
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        attackCoef = onePoleCoef(0.0001f, fs);
        setDecay(0.1f);
    }

    void setDecay(float tauSeconds) { fall = decayCoef(tauSeconds, fs); }

    float process(float pulse)
    {
        smoothed += (pulse - smoothed) * attackCoef;
        v = smoothed > v ? smoothed : v * fall;
        return v;
    }

    float value() const { return v; }
    void reset() { v = smoothed = 0.0f; }

private:
    float fs = 48000, attackCoef = 1, fall = 0;
    float smoothed = 0, v = 0;
};

//
// The pulse shaper in front of a resonator: a high shelf that passes the pulse's rising
// edge whole and a fraction of its flat top, then a diode that shaves most of the swing
// below ground off the falling edge, so that edge does not cancel the rising one's kick.
//
class PulseShaper
{
public:
    void prepare(float sampleRate, float tauSeconds = 0.0003f)
    {
        coef = onePoleCoef(tauSeconds, sampleRate);
    }

    float process(float p)
    {
        const float settled = rc.lowPass(p, coef);
        const float s = (p - settled) + flatTop * settled;
        return s >= 0.0f ? s : s * diodeLeak;
    }

    void reset() { rc.reset(); }

private:
    static constexpr float flatTop = 0.3f;
    static constexpr float diodeLeak = 0.15f;

    OnePole rc;
    float coef = 1;
};

} // namespace tr909
