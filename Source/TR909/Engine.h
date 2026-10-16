// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "BassDrum.h"
#include "Cymbal.h"
#include "Dsp.h"
#include "HandClap.h"
#include "HiHat.h"
#include "RimShot.h"
#include "Settings.h"
#include "SnareDrum.h"
#include "Toms.h"

#include <atomic>
#include <cstdint>

namespace tr909
{

// The eleven sounds, in the order of the 909's instrument selector.
namespace Voice
{
    enum { BD, SD, LT, MT, HT, RS, CP, CH, OH, CC, RC, count };
}

struct VoiceInfo
{
    const char* code;    // as printed on the panel
    const char* name;
    int note;            // the MIDI note that plays it, from the General MIDI drum map
};

const VoiceInfo& voiceInfo(int voice);

// The voice a MIDI note plays, or -1. The GM drum map, with its near neighbours folded on
// to the nearest 909 sound (kick 1 and 2, both snares, the six toms in pairs, the crashes,
// the rides and the ride bell).
int voiceForNote(int note);

//
// One TR-909's sound circuits, from the trigger to the master volume. Struck from MIDI:
// a note fires its instrument's trigger, and a velocity of accentVelocity or more makes it
// an accented one. There is nothing to release - every sound runs out on its own, as on
// the hardware - so note-offs are ignored.
//
// Everything is called from the audio thread, except construction (which builds the ROMs)
// and hitCount(), which is safe from any.
//
class Engine
{
public:
    static constexpr int accentVelocity = 101;   // anything over 100 is accented

    Engine();

    void prepare(double sampleRate);
    void setSettings(const Settings& s);

    void noteOn(int note, int velocity);
    void trigger(int voice, bool accented);
    void allSoundOff();   // not something a 909 can do, but MIDI expects it

    void render(float* out, int numSamples);

    // bumped on every hit, for the panel's lights
    uint32_t hitCount(int voice) const { return hits[voice].load(std::memory_order_relaxed); }

private:
    // the circuits; the two hihats share one
    enum Circuit { BdC, SdC, LtC, MtC, HtC, RimC, ClapC, HatC, CrashC, RideC, circuits };

    struct Gain
    {
        float target = 0, now = 0;
        float next(float coef) { now += (target - now) * coef; return now; }
    };

    float fs = 48000;
    Settings settings;
    Random rng;

    BassDrum bd;
    SnareDrum sd;
    Tom lt { TomSize::Low }, mt { TomSize::Mid }, ht { TomSize::High };
    RimShot rim;
    HandClap clap;
    HiHat hat;
    Cymbal crash { crashRom }, ride { rideRom };

    bool awake[circuits] {};
    Gain level[circuits];
    Gain master;
    float gainCoef = 0.01f;

    float dcX1 = 0, dcY1 = 0, dcCoef = 0.999f;

    std::atomic<uint32_t> hits[Voice::count] {};
};

} // namespace tr909
