// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

//
// The ROMs' contents: stand-ins for the 909's recordings of its hihats, crash and ride,
// generated here and stored as the 909 stores its own, 6-bit.
//
// Each is made from nothing but white noise and sine waves, then shaped to a measured
// description of the sound (RomProfiles.h):
//
//  1. The source: white noise, with a sine at each of the profile's steady pitches, each as
//     far above the noise as the profile says, early, middle and late.
//  2. Shaping: a short-time Fourier transform, frames of 512 a quarter-frame apart. In each
//     frame the energy in each of 18 bands is set to the profile's level at that moment,
//     with the gain eased between band centres so no band edge is heard. Peaks and noise in
//     a band are scaled together, so each pitch keeps its standing above the noise.
//  3. The onset, a millisecond at a time over the first 40 ms, set to the profile's.
//  4. The last stretch faded to nothing, then 6 bits.
//
// The noise is seeded, so every build makes the same ROMs, as a ROM should be.
//

#include "Rom.h"
#include "RomProfiles.h"

#include <algorithm>
#include <complex>

namespace tr909
{

namespace
{

constexpr float romClock = 48000.0f;

constexpr float bandEdges[] = { 0, 200, 400, 630, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300,
                                8000, 10000, 12500, 16000, 20000, 24000 };
constexpr int bandCount = (int) std::size(bandEdges) - 1;

// the three moments a partial's standing above the noise is given at
constexpr float partialTimes[3] = { 0.09f, 0.375f, 0.7f };

using Bins = std::vector<std::complex<float>>;

void fft(Bins& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float angle = (inverse ? 2.0f : -2.0f) * (float) pi / (float) len;
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= step;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (float) n;
}

// Linear between points, held beyond the ends.
float between(const float* xs, const float* ys, size_t count, float x)
{
    if (x <= xs[0]) return ys[0];
    for (size_t i = 1; i < count; ++i)
        if (x < xs[i])
            return ys[i - 1] + (ys[i] - ys[i - 1]) * (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
    return ys[count - 1];
}

// The profile's level in one band at time t, in dB.
float levelAt(const RomProfile& p, int band, float t)
{
    const auto& ts = p.times;
    if (t <= ts.front()) return p.levels.front()[(size_t) band];
    for (size_t i = 1; i < ts.size(); ++i)
        if (t < ts[i])
        {
            const float a = p.levels[i - 1][(size_t) band], b = p.levels[i][(size_t) band];
            return a + (b - a) * (t - ts[i - 1]) / (ts[i] - ts[i - 1]);
        }
    return p.levels.back()[(size_t) band];
}

// 1. white noise and the partials
std::vector<float> source(const RomProfile& p, size_t length, Random& rng)
{
    // In a Hann-windowed transform of M points, a sine of amplitude A peaks at A^2 M / 6 times
    // unit white noise's mean bin power, and the median is ln 2 of the mean. The profiles give
    // peak over median, measured with M = 8192.
    std::vector<float> x(length);
    for (auto& v : x) v = rng.white();

    for (const auto& q : p.partials)
    {
        const double inc = q.hz / romClock;
        double phase = rng.uniform();
        for (size_t n = 0; n < length; ++n)
        {
            const float above = between(partialTimes, q.above, 3, (float) n / romClock);
            const float amp = std::sqrt(6.0f * 0.6931f * std::pow(10.0f, above / 10.0f) / 8192.0f);
            x[n] += amp * (float) std::sin(2.0 * pi * phase);
            phase += inc;
            if (phase >= 1.0) phase -= 1.0;
        }
    }
    return x;
}

// 2. each frame's bands to the profile's levels
std::vector<float> shape(const RomProfile& p, const std::vector<float>& x)
{
    constexpr size_t frame = 512, hop = frame / 4;
    const size_t length = x.size();

    std::vector<float> window(frame);
    float windowEnergy = 0.0f;
    for (size_t i = 0; i < frame; ++i)
    {
        window[i] = 0.5f - 0.5f * std::cos(2.0f * (float) pi * (float) i / (float) frame);
        windowEnergy += window[i] * window[i];
    }

    // each bin's place between the band centres, on a log scale
    float centre[bandCount];
    for (int b = 0; b < bandCount; ++b)
        centre[b] = std::log(b == 0 ? 100.0f : std::sqrt(bandEdges[b] * bandEdges[b + 1]));
    std::vector<int> bandOf(frame / 2 + 1);
    for (size_t k = 0; k <= frame / 2; ++k)
    {
        const float f = (float) k * romClock / (float) frame;
        int b = 0;
        while (b + 1 < bandCount && f >= bandEdges[b + 1]) ++b;
        bandOf[k] = b;
    }

    // padded a frame each side, so the first and last samples are shaped like the rest
    std::vector<float> in(length + 2 * frame, 0.0f), out(length + 2 * frame, 0.0f);
    std::copy(x.begin(), x.end(), in.begin() + frame);

    Bins bins(frame);
    for (size_t s = 0; s + frame <= in.size(); s += hop)
    {
        for (size_t i = 0; i < frame; ++i) bins[i] = in[s + i] * window[i];
        fft(bins, false);

        float energy[bandCount] {}, count[bandCount] {};
        for (size_t k = 1; k <= frame / 2; ++k)
        {
            energy[bandOf[k]] += std::norm(bins[k]) / windowEnergy;
            count[bandOf[k]] += 1.0f;
        }

        // A frame reaching back before the hit holds only part of the sound, and is asked for
        // only that part of the level - as the profile's own first frames were measured, half
        // in the silence before the hit. Asked for all of it, the sliver would be boosted.
        const float t = ((float) s + frame / 2.0f - (float) frame) / romClock;
        float inside = 0.0f;
        for (size_t i = 0; i < frame; ++i)
            if (s + i >= frame) inside += window[i] * window[i];
        inside /= windowEnergy;
        if (inside < 1.0e-3f) continue;

        float logGain[bandCount];
        for (int b = 0; b < bandCount; ++b)
        {
            const float target = inside * std::pow(10.0f, levelAt(p, b, t) / 10.0f);
            const float have = std::max(energy[b] / std::max(count[b], 1.0f), 1.0e-12f);
            logGain[b] = 0.5f * std::log(target / have);
        }

        bins[0] = 0.0f;
        for (size_t k = 1; k <= frame / 2; ++k)
        {
            const float lf = std::log((float) k * romClock / (float) frame);
            const float g = std::exp(between(centre, logGain, bandCount, lf));
            bins[k] *= g;
            if (k < frame / 2) bins[frame - k] *= g;
        }

        fft(bins, true);
        for (size_t i = 0; i < frame; ++i) out[s + i] += bins[i].real() * window[i] / 1.5f;
    }

    return std::vector<float>(out.begin() + frame, out.begin() + (long long) (frame + length));
}

// 3. the first 40 ms, a millisecond at a time
void setOnset(const RomProfile& p, std::vector<float>& x)
{
    const size_t ms = (size_t) (romClock / 1000.0f);
    auto rms = [&x] (size_t a, size_t b)
    {
        double s = 0;
        for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
        return (float) std::sqrt(s / (double) (b - a));
    };

    const float ref = rms(30 * ms, 40 * ms);
    float gain[40], at[40];
    for (size_t m = 0; m < 40; ++m)
    {
        const float want = ref * std::pow(10.0f, p.onset[m] / 20.0f);
        gain[m] = clamp(want / std::max(rms(m * ms, (m + 1) * ms), 1.0e-9f), 0.0f, 4.0f);
        at[m] = ((float) m + 0.5f) * (float) ms;
    }
    for (size_t n = 0; n < 40 * ms; ++n)
    {
        // eased into the unchanged sound over the last few milliseconds
        const float g = between(at, gain, 40, (float) n);
        const float blend = clamp(((float) n - 35.0f * (float) ms) / (5.0f * (float) ms), 0.0f, 1.0f);
        x[n] *= g + (1.0f - g) * blend;
    }
}

// 4. faded out, then into 6 bits with the loudest moment at full scale
Rom store(std::vector<float> x)
{
    const size_t fade = x.size() / 20;
    for (size_t i = 0; i < fade; ++i)
        x[x.size() - fade + i] *= 0.5f + 0.5f * std::cos((float) pi * (float) i / (float) fade);

    float top = 0.0f;
    for (float v : x) top = std::max(top, std::fabs(v));

    Rom rom;
    rom.clock = romClock;
    rom.data.reserve(x.size());
    for (float v : x)
        rom.data.push_back((int8_t) std::clamp(std::lround(v / top * 31.0f), -32L, 31L));
    return rom;
}

Rom build(const RomProfile& p, uint32_t seed)
{
    Random rng(seed);
    auto x = shape(p, source(p, (size_t) (p.seconds * romClock), rng));
    setOnset(p, x);
    return store(std::move(x));
}

} // namespace

const Rom& closedHatRom()
{
    static const Rom rom = build(closedHatProfile, 0x909A0001u);
    return rom;
}

const Rom& openHatRom()
{
    static const Rom rom = build(openHatProfile, 0x909A0004u);
    return rom;
}

const Rom& crashRom()
{
    static const Rom rom = build(crashProfile, 0x909A0002u);
    return rom;
}

const Rom& rideRom()
{
    static const Rom rom = build(rideProfile, 0x909A0003u);
    return rom;
}

} // namespace tr909
