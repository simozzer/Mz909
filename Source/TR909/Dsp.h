// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include <cmath>
#include <cstdint>

namespace tr909
{

constexpr double pi = 3.14159265358979323846;

inline float clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// A linear pot setting an RC time or a frequency through an exponential law: equal travel,
// equal ratio.
inline float expLaw(float x, float lo, float hi) { return lo * std::pow(hi / lo, clamp(x, 0.0f, 1.0f)); }

// A level pot, audio taper as the 909's are: half way round is about -12 dB.
inline float taper(float panel)
{
    const float x = clamp(panel / 10.0f, 0.0f, 1.0f);
    return x * x;
}

// One-pole RC coefficient for a time constant in seconds.
inline float onePoleCoef(float tauSeconds, float sampleRate)
{
    return tauSeconds <= 0.0f ? 1.0f : 1.0f - std::exp(-1.0f / (tauSeconds * sampleRate));
}

// The per-sample multiplier of an RC discharge.
inline float decayCoef(float tauSeconds, float sampleRate)
{
    return tauSeconds <= 0.0f ? 0.0f : std::exp(-1.0f / (tauSeconds * sampleRate));
}

// The time constant of an RC low pass with its corner at f.
inline float tauOf(float hz) { return 1.0f / (2.0f * (float) pi * hz); }

// tanh, close enough for a transistor and a good deal cheaper. Exact at 0, within a percent
// to |x| = 3, and saturating to +-1 beyond.
inline float softClip(float x)
{
    if (x > 3.0f)  return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// softClip(x) / x: the small-signal gain of a saturating stage where it is working.
inline float softClipGain(float x)
{
    if (std::fabs(x) < 1.0e-4f) return 1.0f;
    return softClip(x) / x;
}

class Random
{
public:
    explicit Random(uint32_t seed = 0x2545F491u) : state(seed ? seed : 1u) {}

    uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float uniform() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0, 1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }                          // [-1, 1)

    // White noise at unit deviation. Uniform rather than gaussian: it is filtered before
    // anyone hears it, and the ear cannot tell.
    float white() { return bipolar() * 1.7320508f; }

private:
    uint32_t state;
};

//
// A state-variable filter, solved with zero-delay feedback (the topology-preserving form),
// so it stays in tune to the top of the band and does not blow up when its frequency is
// moved every sample - which the bass drum and toms do on every hit.
//
// The band-pass output, bp, is the unnormalised one: a step of height h into it rings at
// amplitude h, and its gain at the centre is Q. bandNorm() is the same output at unity gain
// at the centre, for when it is a filter rather than a resonator.
//
class Svf
{
public:
    void prepare(float sampleRate) { piOverFs = (float) pi / sampleRate; nyquistish = 0.49f * sampleRate; }

    void setup(float hz, float q)
    {
        g = std::tan(piOverFs * clamp(hz, 1.0f, nyquistish));
        setDamping(1.0f / q);
    }

    // k = 1/Q. Together with setup's frequency, without the tan when only k moves.
    void setDamping(float damping)
    {
        k = damping;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void tune(float hz, float damping)
    {
        g = std::tan(piOverFs * clamp(hz, 1.0f, nyquistish));
        setDamping(damping);
    }

    float process(float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
        return v1;
    }

    float bandNorm() const { return k * bp; }

    void reset() { ic1 = ic2 = lp = bp = hp = 0.0f; }

    // how much is still ringing, for deciding a voice has gone quiet
    float energy() const { return std::fabs(ic1) + std::fabs(ic2); }

    float lp = 0, bp = 0, hp = 0;

private:
    float piOverFs = 0, nyquistish = 20000;
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;
};

// An RC low pass, with the high pass as what it leaves.
struct OnePole
{
    float z = 0;

    float lowPass(float x, float coef)  { z += (x - z) * coef; return z; }
    float highPass(float x, float coef) { return x - lowPass(x, coef); }
    void reset() { z = 0; }
};

} // namespace tr909
