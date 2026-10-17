// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Dsp.h"

#include <cstdint>
#include <vector>

namespace tr909
{

//
// The 909's hihats and cymbals are not circuits but recordings: 6-bit samples in ROM, read
// out by a counter on its own clock into a 6-bit DAC. Sixty-four levels is coarse - the
// quiet end of a cymbal's decay is only a few steps tall - and that grain is a large part
// of the 909's metal.
//
// Roland's recordings are Roland's, so these ROMs hold stand-ins, synthesized here once and
// then stored exactly as the 909 stores its own: 6-bit, at a fixed clock. Everything after
// the ROM - the clock, the DAC, the VCAs - is the 909's way of playing them.
//
struct Rom
{
    std::vector<int8_t> data;   // -32 to 31
    float clock = 48000.0f;     // hertz, at TUNE's centre
};

// Built on first use, the same on every run - a ROM does not change between hits.
const Rom& closedHatRom();
const Rom& openHatRom();
const Rom& crashRom();
const Rom& rideRom();

//
// A DAC step as the host can carry it: the step through a low pass just under the host's
// Nyquist (a Kaiser-windowed sinc, integrated), less the step itself. Added round each step,
// it turns a staircase into the band-limited one an ideal converter and a good recorder
// would give: flat to about 0.42 of the host's rate, little folding back from above.
//
class StepResidual
{
public:
    static constexpr int half = 16;          // taps either side of the step; the latency
    static constexpr int perSample = 64;     // the table's resolution

    static const StepResidual& get()
    {
        static const StepResidual r;
        return r;
    }

    // t in samples from the step, -half to half
    float at(float t) const
    {
        const float u = (t + (float) half) * (float) perSample;
        const int i = (int) u;
        if (i < 0 || i + 1 >= (int) table.size()) return 0.0f;
        const float f = u - (float) i;
        return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
    }

private:
    StepResidual()
    {
        constexpr double cutoff = 0.47, beta = 6.0;   // cycles per sample; the window's shape
        auto bessel0 = [] (double x)
        {
            double sum = 1, term = 1;
            for (int k = 1; k < 30; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
            return sum;
        };

        const int n = 2 * half * perSample + 1;
        std::vector<double> step(n);
        double total = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (i - half * perSample) / perSample;
            const double r = t / half;
            const double sinc = t == 0 ? 2 * cutoff : std::sin(2 * pi * cutoff * t) / (pi * t);
            total += sinc * bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / bessel0(beta);
            step[(size_t) i] = total;
        }
        table.resize((size_t) n);
        for (int i = 0; i < n; ++i)
            table[(size_t) i] = (float) (step[(size_t) i] / total - (i >= half * perSample ? 1.0 : 0.0));
    }

    std::vector<float> table;
};

//
// A ROM's counter and DAC. The DAC holds each sample until the next, so its output is a
// staircase whose steps fall at the ROM's clock, not the host's. Each step is placed at
// its exact sub-sample time and band-limited there, so the stairs are kept but nothing
// above the host's Nyquist folds back. That costs StepResidual::half samples of latency.
//
class RomPlayer
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        StepResidual::get();
        stop();
    }

    // the clock, as a ratio of the ROM's own; from the next start()
    void setSpeed(float ratio) { speed = ratio; }

    // From the top of a ROM. A new one takes over the DAC from wherever it was, so one
    // player switching ROMs is one DAC, as the hihats share.
    void start(const Rom& r)
    {
        rom = &r;
        inc = (double) rom->clock * speed / fs;
        pos = 0.0;
        index = -1;
        playing = true;
    }

    void stop()
    {
        playing = false;
        index = -1;
        held = 0.0f;
        drain = 0;
        for (auto& c : corrections) c = 0.0f;
        for (auto& s : stairs) s = 0.0f;
    }

    float next()
    {
        // ring slot `head` is the moment going out now, `half` samples ago
        constexpr int half = StepResidual::half, span = 2 * half;

        if (playing)
        {
            pos += inc;
            const int size = (int) rom->data.size();
            const auto& residual = StepResidual::get();

            while ((double) (index + 1) <= pos)
            {
                ++index;
                const float v = index < size ? (float) rom->data[(size_t) index] * (1.0f / 32.0f) : 0.0f;
                const float h = v - held;
                held = v;

                if (h != 0.0f)
                {
                    // the step fell x samples before now; spread it over the moments round it
                    const float x = (float) ((pos - (double) index) / inc);
                    for (int j = 0; j < span; ++j)
                        corrections[(head + j) % span] += h * residual.at((float) (j - half) + x);
                }

                if (index >= size)
                {
                    playing = false;
                    drain = span;
                    break;
                }
            }
        }
        else if (drain > 0)
            --drain;

        stairs[(head + half) % span] = held;
        const float out = stairs[head] + corrections[head];
        corrections[head] = 0.0f;
        head = (head + 1) % span;
        return out;
    }

    bool finished() const { return ! playing && drain == 0; }

private:
    const Rom* rom = nullptr;
    float fs = 48000, speed = 1;
    double inc = 0, pos = 0;
    int index = -1;
    bool playing = false;
    float held = 0;
    int head = 0, drain = 0;
    float corrections[2 * StepResidual::half] {};
    float stairs[2 * StepResidual::half] {};
};

} // namespace tr909
