// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

//
// The engine, checked on its own: every sound's pitch and character, accent, the sweeps,
// the decays and the knobs that set them, the hats' choke, the ROMs and their clock,
// independence from the sample rate, and that no setting of any control makes it blow up.
// Links no JUCE.
//
//     DspCheck                 run the checks
//     DspCheck --wav <dir>     and also write demo renders there to listen to
//

#define _CRT_SECURE_NO_WARNINGS   // fopen, for the demo renders

#include "Engine.h"
#include "RomProfiles.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace tr909;

namespace
{

double rate = 48000.0;
int failures = 0;

void check(bool ok, const char* what, const std::string& detail)
{
    std::printf("  %s  %-46s %s\n", ok ? "ok  " : "FAIL", what, detail.c_str());
    if (! ok) ++failures;
}

std::string fmt(const char* f, double a, double b = 0, double c = 0)
{
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

// every level up, so each sound is heard at full
Settings loud()
{
    Settings s;
    s.bdLevel = s.sdLevel = s.ltLevel = s.mtLevel = s.htLevel = 10;
    s.rsLevel = s.cpLevel = s.hhLevel = s.crLevel = s.rdLevel = 10;
    s.volume = 7;
    return s;
}

std::vector<float> hit(int voice, const Settings& s, double seconds, bool accent = false)
{
    Engine e;
    e.prepare(rate);
    e.setSettings(s);
    e.trigger(voice, accent);
    std::vector<float> v((size_t) (seconds * rate));
    e.render(v.data(), (int) v.size());
    return v;
}

size_t at(double seconds) { return (size_t) (seconds * rate); }

double peak(const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double p = 0;
    for (size_t i = from; i < to; ++i) p = std::max(p, (double) std::fabs(v[i]));
    return p;
}

double rms(const std::vector<float>& v, size_t from = 0, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double s = 0;
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return std::sqrt(s / (double) std::max<size_t>(1, to - from));
}

bool finite(const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite(x)) return false;
    return true;
}

// Frequency from rising zero crossings, interpolated, between two times.
double frequency(const std::vector<float>& v, double from, double to)
{
    double first = -1, last = -1;
    int count = 0;
    const size_t end = std::min(v.size(), at(to));

    for (size_t i = at(from) + 1; i < end; ++i)
    {
        if (v[i - 1] < 0 && v[i] >= 0)
        {
            const double t = (double) (i - 1) + v[i - 1] / (double) (v[i - 1] - v[i]);
            if (first < 0) first = t; else ++count;
            last = t;
        }
    }

    return count > 0 ? count * rate / (last - first) : 0.0;
}

// The same, with the noise and upper partials taken off first - two RC low passes at
// `below` - for drums with more than one thing going on.
double ringFrequency(const std::vector<float>& v, double from, double to, double below)
{
    const float c = 1.0f - std::exp(-2.0f * (float) pi * (float) below / (float) rate);
    std::vector<float> f(v.size());
    float a = 0, b = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        a += (v[i] - a) * c;
        b += (a - b) * c;
        f[i] = b;
    }
    return frequency(f, from, to);
}

// The strongest frequency between lo and hi, to the hertz, over a stretch.
double peakFrequency(const std::vector<float>& v, double lo, double hi, double from, double to);

// The length of the first half-cycle, from the moment the sound starts.
double firstHalfCycle(const std::vector<float>& v)
{
    const double threshold = peak(v) * 0.02;
    size_t start = 0;
    while (start < v.size() && std::fabs(v[start]) < threshold) ++start;
    const bool positive = v[start] > 0;
    size_t i = start;
    while (i < v.size() && (v[i] > 0) == positive) ++i;
    return (double) (i - start) / rate;
}

// The level at one frequency, by Goertzel, over a stretch.
double toneAt(const std::vector<float>& v, double hz, double from, double to)
{
    const double w = 2.0 * pi * hz / rate, c = 2.0 * std::cos(w);
    double s1 = 0, s2 = 0;
    const size_t a = at(from), b = std::min(v.size(), at(to));
    for (size_t i = a; i < b; ++i)
    {
        const double s0 = v[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / (double) std::max<size_t>(1, b - a);
}

double peakFrequency(const std::vector<float>& v, double lo, double hi, double from, double to)
{
    double best = lo, level = -1;
    for (double hz = lo; hz <= hi; hz += 1.0)
    {
        const double l = toneAt(v, hz, from, to);
        if (l > level) { level = l; best = hz; }
    }
    return best;
}

// How bright: the level of the first difference against the level itself.
double brightness(const std::vector<float>& v, size_t from, size_t to)
{
    double d = 0, s = 0;
    for (size_t i = from + 1; i < std::min(to, v.size()); ++i)
    {
        d += (double) (v[i] - v[i - 1]) * (v[i] - v[i - 1]);
        s += (double) v[i] * v[i];
    }
    return s > 0 ? std::sqrt(d / s) : 0;
}

// Seconds until the level, in 5 ms windows, stays below the peak window by `db`.
double ringTime(const std::vector<float>& v, double db)
{
    const size_t w = at(0.005);
    std::vector<double> win;
    for (size_t i = 0; i + w <= v.size(); i += w) win.push_back(rms(v, i, i + w));
    const double top = *std::max_element(win.begin(), win.end());
    const double floor = top * std::pow(10.0, -db / 20.0);
    size_t lastAbove = 0;
    for (size_t i = 0; i < win.size(); ++i) if (win[i] > floor) lastAbove = i;
    return (double) (lastAbove + 1) * 0.005;
}

//------------------------------------------------------------------------------ the checks

void everySound()
{
    std::printf("\nevery sound\n");

    for (int voice = 0; voice < Voice::count; ++voice)
    {
        const auto& info = voiceInfo(voice);
        const auto plain = hit(voice, loud(), 4.0);
        const auto accented = hit(voice, loud(), 4.0, true);

        const double p = peak(plain), pa = peak(accented);
        const double tail = rms(plain, at(3.5));
        const bool ok = finite(plain) && finite(accented) && p > 0.05 && pa < 1.5 && tail < 1.0e-4;

        std::string what = std::string(info.code) + " " + info.name + ": sounds, rings out";
        check(ok, what.c_str(), fmt("peak %.2f, accented %.2f, tail %.1e", p, pa, tail));

        what = std::string(info.code) + " accent is louder";
        check(pa > p * 1.15, what.c_str(), fmt("%+.1f dB", 20 * std::log10(pa / p)));
    }
}

void accentFromVelocity()
{
    std::printf("\naccent from velocity\n");

    auto played = [] (int velocity)
    {
        Engine e;
        e.prepare(rate);
        e.setSettings(loud());
        e.noteOn(38, velocity);
        std::vector<float> v(at(0.3));
        e.render(v.data(), (int) v.size());
        return peak(v);
    };

    const double soft = played(1), at100 = played(100), at101 = played(101), hard = played(127);
    check(at100 == soft, "100 and below is unaccented", fmt("peak %.3f at 1, %.3f at 100", soft, at100));
    check(at101 > at100 * 1.15 && at101 == hard, "above 100 is accented", fmt("peak %.3f at 101, %.3f at 127", at101, hard));
}

void bassDrum()
{
    std::printf("\nthe bass drum\n");

    const auto v = hit(Voice::BD, loud(), 2.0);
    const double body = frequency(v, 0.15, 0.4);
    check(std::fabs(body - 50.0) < 2.5, "settles at about 50 Hz", fmt("%.1f Hz", body));

    // the sweep: a half-cycle at 50 Hz is 10 ms; starting several times higher, the first is
    // much shorter
    const double half = firstHalfCycle(v);
    check(half < 0.005, "starts high and sweeps down", fmt("first half-cycle %.1f ms (10 at 50 Hz)", half * 1000));

    Settings s = loud();
    s.bdTune = 0;  const double tight = frequency(hit(Voice::BD, s, 0.2), 0.02, 0.06);
    s.bdTune = 10; const double deep = frequency(hit(Voice::BD, s, 0.2), 0.02, 0.06);
    check(deep > tight * 1.5, "TUNE: a short drop to a long sweep", fmt("%.0f Hz -> %.0f Hz, 20-60 ms in", tight, deep));

    s = loud();
    s.bdAttack = 0;  const double soft = brightness(hit(Voice::BD, s, 0.05), 0, at(0.003));
    s.bdAttack = 10; const double click = brightness(hit(Voice::BD, s, 0.05), 0, at(0.003));
    check(click > soft * 1.5, "ATTACK brings in the beater", fmt("attack brightness %.3f -> %.3f", soft, click));

    s = loud();
    s.bdDecay = 0;  const double shortest = ringTime(hit(Voice::BD, s, 4.0), 40);
    s.bdDecay = 10; const double longest = ringTime(hit(Voice::BD, s, 4.0), 40);
    check(shortest < 0.45 && longest > 1.4, "DECAY: a thump to a long boom", fmt("%.2f s to %.2f s (-40 dB)", shortest, longest));

    // accent: a taller trigger charges the pitch envelope higher
    const double accented = firstHalfCycle(hit(Voice::BD, loud(), 0.2, true));
    check(accented < half, "an accent sweeps from higher", fmt("first half-cycle %.2f ms, %.2f ms plain", accented * 1000, half * 1000));
}

void snare()
{
    std::printf("\nthe snare\n");

    Settings s = loud();
    s.sdSnappy = 0;

    s.sdTune = 0;  const double down = ringFrequency(hit(Voice::SD, s, 0.3), 0.03, 0.1, 220);
    s.sdTune = 10; const double up = ringFrequency(hit(Voice::SD, s, 0.3), 0.03, 0.1, 350);
    check(std::fabs(down / (185 * 0.8) - 1) < 0.05 && std::fabs(up / down - 1.5625) < 0.08,
          "TUNE moves the head", fmt("%.1f Hz to %.1f Hz", down, up));

    s = loud();
    s.sdSnappy = 0;  const double dry = brightness(hit(Voice::SD, s, 0.3), at(0.01), at(0.1));
    s.sdSnappy = 10; const double snares = brightness(hit(Voice::SD, s, 0.3), at(0.01), at(0.1));
    check(snares > dry * 3, "SNAPPY brings in the snares", fmt("brightness %.3f -> %.3f", dry, snares));

    s = loud();
    s.sdSnappy = 10;
    s.sdTone = 0;  const double dull = brightness(hit(Voice::SD, s, 0.3), at(0.01), at(0.15));
    s.sdTone = 10; const double bright = brightness(hit(Voice::SD, s, 0.3), at(0.01), at(0.15));
    check(bright > dull * 1.3, "TONE brightens the snares", fmt("brightness %.3f -> %.3f", dull, bright));
}

void toms()
{
    std::printf("\nthe toms\n");

    struct Case { int voice; double hz; };
    for (const Case c : { Case { Voice::LT, 100 }, Case { Voice::MT, 140 }, Case { Voice::HT, 200 } })
    {
        const auto v = hit(c.voice, loud(), 0.5);
        const double f = ringFrequency(v, 0.12, 0.28, c.hz * 1.5);
        const std::string what = std::string(voiceInfo(c.voice).name) + " at TUNE 5";
        check(std::fabs(f / c.hz - 1.0) < 0.03, what.c_str(), fmt("%.1f Hz (nominal %.0f)", f, c.hz));
    }

    {
        const auto v = hit(Voice::LT, loud(), 0.5);
        const double start = ringFrequency(v, 0.0, 0.03, 300), settled = ringFrequency(v, 0.12, 0.3, 150);
        check(start > settled * 1.1, "a tom's pitch drops as it strikes", fmt("%.1f Hz -> %.1f Hz", start, settled));
    }

    Settings s = loud();
    s.mtTune = 0;  const double down = ringFrequency(hit(Voice::MT, s, 0.5), 0.12, 0.28, 150);
    s.mtTune = 10; const double up = ringFrequency(hit(Voice::MT, s, 0.5), 0.12, 0.28, 300);
    check(std::fabs(up / down - 2.0) < 0.1, "TUNE spans an octave", fmt("%.1f Hz to %.1f Hz", down, up));

    s = loud();
    s.htDecay = 0;  const double shortest = ringTime(hit(Voice::HT, s, 2.0), 40);
    s.htDecay = 10; const double longest = ringTime(hit(Voice::HT, s, 2.0), 40);
    check(longest > shortest * 3, "DECAY", fmt("%.2f s to %.2f s (-40 dB)", shortest, longest));
}

void rimAndClap()
{
    std::printf("\nrim shot and hand clap\n");

    const auto rs = hit(Voice::RS, loud(), 0.2);
    const double r500 = toneAt(rs, 500, 0, 0.02), r1700 = toneAt(rs, 1700, 0, 0.02), rOff = toneAt(rs, 1050, 0, 0.02);
    check(r500 > rOff && r1700 > rOff, "rim shot: 500 Hz and 1.7 kHz", fmt("%.4f, %.4f against %.4f at 1.05 kHz", r500, r1700, rOff));
    check(ringTime(rs, 40) < 0.06, "rim shot is a crack", fmt("%.3f s (-40 dB)", ringTime(rs, 40)));

    const auto cp = hit(Voice::CP, loud(), 0.8);
    const size_t w = at(0.001);
    std::vector<double> env;
    for (size_t i = 0; i + w <= at(0.05); i += w) env.push_back(rms(cp, i, i + w));
    int bursts = 0;
    for (size_t i = 1; i + 1 < env.size(); ++i)
        if (env[i] > env[i - 1] * 1.8 && env[i] > peak(cp) * 0.08) ++bursts;
    check(bursts >= 3, "hand clap: several hands, then a tail", fmt("%.0f bursts in 50 ms, tail %.2f s", bursts, ringTime(cp, 40)));
}

void hats()
{
    std::printf("\nthe hihats\n");

    Settings s = loud();
    s.chDecay = 0;  const double chShort = ringTime(hit(Voice::CH, s, 1.0), 40);
    s.chDecay = 10; const double chLong = ringTime(hit(Voice::CH, s, 1.0), 40);
    s = loud();
    s.ohDecay = 0;  const double ohShort = ringTime(hit(Voice::OH, s, 2.0), 40);
    s.ohDecay = 10; const double ohLong = ringTime(hit(Voice::OH, s, 2.0), 40);
    const double chDefault = ringTime(hit(Voice::CH, loud(), 1.0), 40), ohDefault = ringTime(hit(Voice::OH, loud(), 2.0), 40);
    check(chLong > chShort * 2.5, "CLOSED DECAY", fmt("%.3f s to %.3f s (-40 dB)", chShort, chLong));
    check(ohLong > ohShort * 3, "OPEN DECAY", fmt("%.3f s to %.3f s (-40 dB)", ohShort, ohLong));
    check(ohDefault > chDefault * 2.5, "open longer than closed, as set", fmt("CH %.3f s, OH %.3f s at the defaults", chDefault, ohDefault));

    // the threshold VCA: an open hat does not fade into the floor, it stops
    {
        const auto v = hit(Voice::OH, loud(), 1.0);
        const double before = rms(v, at(0.40), at(0.42)), after = rms(v, at(0.50), at(0.52));
        check(after < before * 1.0e-3, "the VCA shuts: OH stops dead", fmt("%.0f dB from 400 ms to 500 ms", 20 * std::log10(after / before + 1e-12)));
    }

    // the choke: an open hat, closed 50 ms in
    {
        Engine e;
        e.prepare(rate);
        e.setSettings(loud());
        e.trigger(Voice::OH, false);
        std::vector<float> v(at(0.5));
        e.render(v.data(), (int) at(0.05));
        e.trigger(Voice::CH, false);
        e.render(v.data() + at(0.05), (int) (v.size() - at(0.05)));
        const auto open = hit(Voice::OH, loud(), 0.5);
        const double left = rms(v, at(0.2), at(0.3)), ringing = rms(open, at(0.2), at(0.3));
        check(left < ringing * 0.02, "CH chokes OH", fmt("%.1f dB of the open hat left", 20 * std::log10(left / ringing + 1e-12)));
    }

    // two recordings: open and closed are different from the first sample
    {
        const auto ch = hit(Voice::CH, loud(), 0.03), oh = hit(Voice::OH, loud(), 0.03);
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = 0; i < ch.size(); ++i) { xy += ch[i] * oh[i]; xx += ch[i] * ch[i]; yy += oh[i] * oh[i]; }
        const double c = xy / std::sqrt(xx * yy);
        check(std::fabs(c) < 0.3, "open and closed are different samples", fmt("correlation %.2f over 30 ms", c));
    }
}

// Band energy density at time t, as the profiles were measured: Hann frames of 512, centred
// on t and a quarter-frame either side, at 48 kHz.
std::vector<double> bandLevels(const std::vector<float>& v, double t)
{
    static const double edges[] = { 0, 200, 400, 630, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300,
                                    8000, 10000, 12500, 16000, 20000, 24000 };
    const size_t n = 512;
    std::vector<double> e(std::size(edges) - 1, 0.0), count(e.size(), 0.0);
    for (int d = -1; d <= 1; ++d)
    {
        std::vector<std::complex<double>> x(n);
        const long long c = (long long) at(t) + d * 128 - (long long) n / 2;
        for (size_t i = 0; i < n; ++i)
        {
            const long long j = c + (long long) i;
            const double w = 0.5 - 0.5 * std::cos(2 * pi * (double) i / (double) n);
            x[i] = j >= 0 && j < (long long) v.size() ? v[(size_t) j] * w : 0.0;
        }
        // a plain DFT is quick enough for a check
        for (size_t k = 1; k < n / 2; ++k)
        {
            std::complex<double> s = 0;
            for (size_t i = 0; i < n; ++i) s += x[i] * std::polar(1.0, -2 * pi * (double) (k * i) / (double) n);
            const double f = (double) k * rate / (double) n;
            for (size_t b = 0; b < e.size(); ++b)
                if (f >= edges[b] && f < edges[b + 1]) { e[b] += std::norm(s); count[b] += 1; }
        }
    }
    for (size_t b = 0; b < e.size(); ++b) e[b] = 10 * std::log10(e[b] / std::max(count[b], 1.0) + 1e-20);
    return e;
}

// The ROMs' sounds against the descriptions they were made from: band by band, moment by
// moment, how far the rendered sound strays from the profile's shape (its overall level
// aside). The hats are heard through their VCA, so the VCA's fall is added to theirs.
void profiles()
{
    std::printf("\nthe metal against its measured profiles\n");

    struct Case { int voice; const RomProfile* p; double vcaTau; double upTo; };
    const Case cases[] = {
        { Voice::CH, &closedHatProfile, 0.12, 0.11 },
        { Voice::OH, &openHatProfile,   0.40, 0.40 },
        { Voice::CC, &crashProfile,     0.0,  0.85 },
        { Voice::RC, &rideProfile,      0.0,  0.85 },
    };

    for (const auto& c : cases)
    {
        const auto v = hit(c.voice, loud(), 1.0);
        std::vector<double> diffs;
        for (size_t i = 0; i < c.p->times.size(); ++i)
        {
            const double t = c.p->times[i];
            if (t < 0.01 || t > c.upTo) continue;
            const double g = c.vcaTau > 0 ? std::max(0.0, std::exp(-t / c.vcaTau) - 0.3) / 0.7 : 1.0;
            const auto have = bandLevels(v, t);
            for (size_t b = 1; b < have.size(); ++b)
            {
                const double want = c.p->levels[i][b] + 20 * std::log10(g + 1e-12);
                if (want > -35) diffs.push_back(have[b] - want);
            }
        }
        double mean = 0; for (double d : diffs) mean += d / (double) diffs.size();
        double err = 0; for (double d : diffs) err += (d - mean) * (d - mean) / (double) diffs.size();
        const std::string what = std::string(voiceInfo(c.voice).name) + " matches its profile";
        check(std::sqrt(err) < 3.0, what.c_str(), fmt("%.1f dB rms over %.0f bands and moments", std::sqrt(err), (double) diffs.size()));
    }
}

void cymbals()
{
    std::printf("\nthe ROMs, crash and ride\n");

    struct Named { const char* name; const Rom* rom; };
    for (const Named r : { Named { "closed hihat", &closedHatRom() }, Named { "open hihat", &openHatRom() },
                           Named { "crash", &crashRom() }, Named { "ride", &rideRom() } })
    {
        std::set<int> levels;
        bool inRange = true;
        for (int8_t x : r.rom->data) { levels.insert(x); inRange = inRange && x >= -32 && x <= 31; }
        check(inRange && levels.size() > 50 && r.rom->data.back() == 0, (std::string(r.name) + " ROM is 6-bit, full scale").c_str(),
              fmt("%.0f of 64 levels, %.2f s at %.0f Hz", (double) levels.size(), r.rom->data.size() / r.rom->clock, r.rom->clock));
    }

    // as measured: both ring for most of a second, and stop when the sample does
    for (int voice : { Voice::CC, Voice::RC })
    {
        const auto v = hit(voice, loud(), 2.0);
        const double ring = ringTime(v, 25);
        const std::string what = std::string(voiceInfo(voice).name) + " rings, then ends";
        check(ring > 0.6 && ring < 1.0 && rms(v, at(1.0)) < 1.0e-6, what.c_str(), fmt("%.2f s (-25 dB), silent after 1 s", ring));
    }

    // a sample is a sample: every hit from rest is the same, but for what is left in the
    // mixer's coupling capacitor
    {
        Engine e;
        e.prepare(rate);
        e.setSettings(loud());
        std::vector<float> a(at(0.05)), gap(at(3.0)), b(at(0.05));
        e.render(gap.data(), (int) at(0.1));   // the levels settle from prepare()'s
        e.trigger(Voice::CC, false);
        e.render(a.data(), (int) a.size());
        e.render(gap.data(), (int) gap.size());
        e.trigger(Voice::CC, false);
        e.render(b.data(), (int) b.size());
        double diff = 0;
        for (size_t i = 0; i < a.size(); ++i) diff = std::max(diff, (double) std::fabs(a[i] - b[i]));
        check(diff < 1.0e-6, "every crash the same", fmt("largest difference %.1e", diff));
    }

    // TUNE is the ROM's clock: shorter and higher together, by the same ratio. Pitch is
    // measured on the ride's loudest partial, 7074 Hz at TUNE 5.
    const double clock = 1.47 / 0.68;
    {
        Settings s = loud();
        s.crTune = 0;  const double slow = ringTime(hit(Voice::CC, s, 6.0), 30);
        s.crTune = 10; const double fast = ringTime(hit(Voice::CC, s, 6.0), 30);
        check(std::fabs(slow / fast / clock - 1) < 0.1, "crash TUNE is the clock",
              fmt("%.2fx shorter (clock %.2fx)", slow / fast, clock));
    }
    {
        Settings s = loud();
        s.rdTune = 0;  const auto slow = hit(Voice::RC, s, 6.0);
        s.rdTune = 10; const auto fast = hit(Voice::RC, s, 6.0);
        const double low = peakFrequency(slow, 4790, 4830, 0.02, 0.3), high = peakFrequency(fast, 10380, 10440, 0.02, 0.3);
        const double length = ringTime(slow, 30) / ringTime(fast, 30);
        check(std::fabs(high / low / clock - 1) < 0.02 && std::fabs(length / clock - 1) < 0.1, "ride TUNE is the clock",
              fmt("partial %.0f Hz -> %.0f Hz, %.2fx shorter", low, high, length));
    }
}

void sampleRates()
{
    std::printf("\nother sample rates\n");

    const double saved = rate;
    for (double r : { 44100.0, 96000.0, 192000.0 })
    {
        rate = r;
        const double bdHz = frequency(hit(Voice::BD, loud(), 0.5), 0.15, 0.4);
        const double ltHz = ringFrequency(hit(Voice::LT, loud(), 0.5), 0.12, 0.28, 150);
        const auto rc = hit(Voice::RC, loud(), 0.5);
        const double partial = peakFrequency(rc, 7040, 7110, 0.02, 0.3);
        const auto cc = hit(Voice::CC, loud(), 0.3);
        const bool ok = std::fabs(bdHz - 50) < 2.5 && std::fabs(ltHz / 100 - 1) < 0.03 && std::fabs(partial / 7074 - 1) < 0.005
                     && finite(cc) && peak(cc) < 1.5;
        check(ok, fmt("%.0f Hz", r).c_str(), fmt("BD %.1f Hz, LT %.1f Hz, ride partial %.0f Hz", bdHz, ltHz, partial));
    }
    rate = saved;
}

void stability()
{
    std::printf("\nstability\n");

    // every knob at each end, every sound accented at once, again and again
    double worst = 0;
    bool ok = true;
    for (int corner = 0; corner < 4; ++corner)
    {
        Settings s = loud();
        const float a = (corner & 1) ? 10.0f : 0.0f, b = (corner & 2) ? 10.0f : 0.0f;
        s.accent = 10; s.volume = 10;
        s.bdTune = a; s.bdAttack = b; s.bdDecay = b;
        s.sdTune = a; s.sdTone = b; s.sdSnappy = b;
        s.ltTune = s.mtTune = s.htTune = a; s.ltDecay = s.mtDecay = s.htDecay = b;
        s.chDecay = s.ohDecay = b; s.crTune = s.rdTune = a;

        Engine e;
        e.prepare(rate);
        e.setSettings(s);
        std::vector<float> v(at(0.02));
        for (int rep = 0; rep < 100; ++rep)
        {
            for (int voice = 0; voice < Voice::count; voice += 1 + (rep & 1))
                e.trigger(voice, true);
            e.render(v.data(), (int) v.size());
            ok = ok && finite(v);
            worst = std::max(worst, peak(v));
        }
    }
    // Eleven accented sounds at full volume do sum to well over 1 - that is the mix, not
    // a fault. What matters is that it stays finite and bounded.
    check(ok && worst < 16.0, "everything at once, every extreme", fmt("peak %.2f over 2 s of chaos", worst));

    {
        Engine e;
        e.prepare(rate);
        Settings s = loud();
        s.bdDecay = 10;
        e.setSettings(s);
        std::vector<float> v(at(0.01));
        double p = 0;
        for (int rep = 0; rep < 300; ++rep)
        {
            e.trigger(Voice::BD, true);
            e.render(v.data(), (int) v.size());
            p = std::max(p, peak(v));
        }
        check(std::isfinite(p) && p < 2.0, "a 100 Hz roll on a long bass drum", fmt("peak %.2f", p));
    }
}

void performance()
{
    std::printf("\nperformance\n");

    Engine e;
    e.prepare(rate);
    e.setSettings(loud());
    const int block = 256;
    std::vector<float> v(block);
    const int blocks = (int) (20.0 * rate / block);

    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        // a busy 16th-note pattern at 120 BPM: everything, every step
        if (b % 12 == 0)
            for (int voice = 0; voice < Voice::count; ++voice) e.trigger(voice, (b / 12) % 4 == 0);
        e.render(v.data(), block);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double load = secs / 20.0 * 100.0;
    check(load < 10.0, "all eleven sounds, every 16th", fmt("%.2f%% of one core", load));
}

//------------------------------------------------------------------------------ demos

void writeWav(const std::string& path, const std::vector<float>& v)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (! f) { std::printf("  could not write %s\n", path.c_str()); return; }

    auto u32 = [f] (uint32_t x) { std::fwrite(&x, 4, 1, f); };
    auto u16 = [f] (uint16_t x) { std::fwrite(&x, 2, 1, f); };

    const uint32_t bytes = (uint32_t) v.size() * 2;
    std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1); u32((uint32_t) rate); u32((uint32_t) rate * 2); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(bytes);
    for (float x : v)
    {
        const int16_t s = (int16_t) std::lround(clamp(x, -1.0f, 1.0f) * 32767.0f);
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
    std::printf("  wrote %s\n", path.c_str());
}

// A pattern, one row per sound: x a hit, A an accented one, . nothing - sixteenths.
struct Row { int voice; const char* steps; };

std::vector<float> play(const std::vector<Row>& rows, double bpm, int bars, const Settings& s)
{
    Engine e;
    e.prepare(rate);
    e.setSettings(s);

    const size_t step = (size_t) (rate * 60.0 / bpm / 4.0);
    std::vector<float> v(step * 16 * (size_t) bars + at(2.5));

    size_t pos = 0;
    for (int bar = 0; bar < bars; ++bar)
        for (int i = 0; i < 16; ++i)
        {
            for (const auto& r : rows)
                if (r.steps[i] == 'x' || r.steps[i] == 'A') e.trigger(r.voice, r.steps[i] == 'A');
            e.render(v.data() + pos, (int) step);
            pos += step;
        }
    e.render(v.data() + pos, (int) (v.size() - pos));
    return v;
}

void demos(const std::string& dir)
{
    std::printf("\ndemos\n");

    // every sound in turn, plain then accented
    {
        Engine e;
        e.prepare(rate);
        e.setSettings(loud());
        const size_t gap = at(0.8);
        std::vector<float> v(gap * 2 * Voice::count);
        for (int voice = 0; voice < Voice::count; ++voice)
            for (int a = 0; a < 2; ++a)
            {
                e.trigger(voice, a == 1);
                e.render(v.data() + gap * (size_t) (voice * 2 + a), (int) gap);
            }
        writeWav(dir + "/mz909-every-sound.wav", v);
    }

    {
        Settings s;
        s.volume = 7;
        writeWav(dir + "/mz909-house.wav", play({
            { Voice::BD, "A...x...A...x..." },
            { Voice::CP, "....x.......x..." },
            { Voice::CH, "x.x...x.x.x...x." },
            { Voice::OH, "....x.......x..." },
            { Voice::OH, "..x.......x....." },
            { Voice::CC, "x..............." },
        }, 124, 4, s));
    }

    {
        Settings s;
        s.bdDecay = 7; s.bdAttack = 8; s.bdTune = 3; s.ohDecay = 3; s.volume = 7;
        writeWav(dir + "/mz909-techno.wav", play({
            { Voice::BD, "A...x...A...x..." },
            { Voice::SD, "....x.......x.xA" },
            { Voice::RS, "...x..x....x...." },
            { Voice::OH, "..x...x...x...x." },
            { Voice::RC, "x.x.x.x.x.x.x.x." },
        }, 132, 4, s));
    }

    {
        Settings s;
        s.volume = 7;
        writeWav(dir + "/mz909-fill.wav", play({
            { Voice::BD, "A.......x......." },
            { Voice::SD, "....x.....xxAxxA" },
            { Voice::HT, "x.x............." },
            { Voice::MT, "...x.x.........." },
            { Voice::LT, "......x.x......." },
            { Voice::CH, "x.x.x.x.x.x....." },
        }, 120, 2, s));
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string wavDir;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--wav") == 0 && i + 1 < argc) wavDir = argv[++i];

    std::printf("Mz909 engine check, %.0f Hz\n", rate);

    everySound();
    accentFromVelocity();
    bassDrum();
    snare();
    toms();
    rimAndClap();
    hats();
    cymbals();
    profiles();
    sampleRates();
    stability();
    performance();

    if (! wavDir.empty()) demos(wavDir);

    std::printf("\n%s\n", failures == 0 ? "all checks passed" : (std::to_string(failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
