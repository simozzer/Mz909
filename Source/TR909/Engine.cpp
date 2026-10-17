// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#include "Engine.h"

namespace tr909
{

const VoiceInfo& voiceInfo(int voice)
{
    static const VoiceInfo table[Voice::count] = {
        { "BD", "Bass Drum",    36 },
        { "SD", "Snare Drum",   38 },
        { "LT", "Low Tom",      43 },
        { "MT", "Mid Tom",      47 },
        { "HT", "Hi Tom",       50 },
        { "RS", "Rim Shot",     37 },
        { "CP", "Hand Clap",    39 },
        { "CH", "Closed Hihat", 42 },
        { "OH", "Open Hihat",   46 },
        { "CC", "Crash Cymbal", 49 },
        { "RC", "Ride Cymbal",  51 },
    };
    return table[voice < 0 || voice >= Voice::count ? 0 : voice];
}

int voiceForNote(int note)
{
    switch (note)
    {
        case 35: case 36:                   return Voice::BD;
        case 37:                            return Voice::RS;
        case 38: case 40:                   return Voice::SD;
        case 39:                            return Voice::CP;
        case 41: case 43:                   return Voice::LT;
        case 45: case 47:                   return Voice::MT;
        case 48: case 50:                   return Voice::HT;
        case 42: case 44:                   return Voice::CH;
        case 46:                            return Voice::OH;
        case 49: case 52: case 55: case 57: return Voice::CC;
        case 51: case 53: case 59:          return Voice::RC;
        default:                            return -1;
    }
}

Engine::Engine()
{
    // Build the ROMs here, off the audio thread, rather than on the first hit.
    closedHatRom();
    openHatRom();
    crashRom();
    rideRom();
}

void Engine::prepare(double sampleRate)
{
    fs = (float) sampleRate;

    bd.prepare(fs);
    sd.prepare(fs);
    lt.prepare(fs);
    mt.prepare(fs);
    ht.prepare(fs);
    rim.prepare(fs);
    clap.prepare(fs);
    hat.prepare(fs);
    crash.prepare(fs);
    ride.prepare(fs);

    for (auto& a : awake) a = false;

    gainCoef = onePoleCoef(0.005f, fs);
    dcCoef = 1.0f - 2.0f * (float) pi * 8.0f / fs;
    dcX1 = dcY1 = 0.0f;

    setSettings(settings);
    for (auto& g : level) g.now = g.target;
    master.now = master.target;
}

void Engine::setSettings(const Settings& s)
{
    settings = s;

    bd.set(s.bdTune, s.bdAttack, s.bdDecay);
    sd.set(s.sdTune, s.sdTone, s.sdSnappy);
    lt.set(s.ltTune, s.ltDecay);
    mt.set(s.mtTune, s.mtDecay);
    ht.set(s.htTune, s.htDecay);
    hat.set(s.chDecay, s.ohDecay);
    crash.set(s.crTune);
    ride.set(s.rdTune);

    level[BdC].target    = taper(s.bdLevel);
    level[SdC].target    = taper(s.sdLevel);
    level[LtC].target    = taper(s.ltLevel);
    level[MtC].target    = taper(s.mtLevel);
    level[HtC].target    = taper(s.htLevel);
    level[RimC].target   = taper(s.rsLevel);
    level[ClapC].target  = taper(s.cpLevel);
    level[HatC].target   = taper(s.hhLevel);
    level[CrashC].target = taper(s.crLevel);
    level[RideC].target  = taper(s.rdLevel);

    master.target = 1.4f * taper(s.volume);
}

void Engine::noteOn(int note, int velocity)
{
    const int v = voiceForNote(note);
    if (v >= 0 && velocity > 0)
        trigger(v, velocity >= accentVelocity);
}

void Engine::trigger(int voice, bool accented)
{
    const float h = triggerHeight(accented, settings.accent);

    switch (voice)
    {
        case Voice::BD: bd.trigger(h);              awake[BdC] = true;    break;
        case Voice::SD: sd.trigger(h);              awake[SdC] = true;    break;
        case Voice::LT: lt.trigger(h);              awake[LtC] = true;    break;
        case Voice::MT: mt.trigger(h);              awake[MtC] = true;    break;
        case Voice::HT: ht.trigger(h);              awake[HtC] = true;    break;
        case Voice::RS: rim.trigger(h);             awake[RimC] = true;   break;
        case Voice::CP: clap.trigger(h);            awake[ClapC] = true;  break;
        case Voice::CH: hat.trigger(h, false);      awake[HatC] = true;   break;   // and chokes OH
        case Voice::OH: hat.trigger(h, true);       awake[HatC] = true;   break;
        case Voice::CC: crash.trigger(h);           awake[CrashC] = true; break;
        case Voice::RC: ride.trigger(h);            awake[RideC] = true;  break;
        default: return;
    }

    hits[voice].fetch_add(1, std::memory_order_relaxed);
}

void Engine::allSoundOff()
{
    bd.reset(); sd.reset(); lt.reset(); mt.reset(); ht.reset();
    rim.reset(); clap.reset(); hat.reset(); crash.reset(); ride.reset();
    for (auto& a : awake) a = false;
}

void Engine::render(float* out, int numSamples)
{
    for (int n = 0; n < numSamples; ++n)
    {
        float g[circuits];
        for (int c = 0; c < circuits; ++c) g[c] = level[c].next(gainCoef);

        float sum = 0.0f;
        if (awake[BdC])    sum += g[BdC]    * bd.process(rng);
        if (awake[SdC])    sum += g[SdC]    * sd.process(rng);
        if (awake[LtC])    sum += g[LtC]    * lt.process(rng);
        if (awake[MtC])    sum += g[MtC]    * mt.process(rng);
        if (awake[HtC])    sum += g[HtC]    * ht.process(rng);
        if (awake[RimC])   sum += g[RimC]   * rim.process();
        if (awake[ClapC])  sum += g[ClapC]  * clap.process(rng);
        if (awake[HatC])   sum += g[HatC]   * hat.process();
        if (awake[CrashC]) sum += g[CrashC] * crash.process();
        if (awake[RideC])  sum += g[RideC]  * ride.process();

        // Every so often, put to sleep whatever has rung out - from rest, so a sleeping
        // circuit wakes exactly as a fresh one would.
        if ((n & 31) == 0)
        {
            if (awake[BdC]    && bd.quiet())    { bd.reset();    awake[BdC] = false; }
            if (awake[SdC]    && sd.quiet())    { sd.reset();    awake[SdC] = false; }
            if (awake[LtC]    && lt.quiet())    { lt.reset();    awake[LtC] = false; }
            if (awake[MtC]    && mt.quiet())    { mt.reset();    awake[MtC] = false; }
            if (awake[HtC]    && ht.quiet())    { ht.reset();    awake[HtC] = false; }
            if (awake[RimC]   && rim.quiet())   { rim.reset();   awake[RimC] = false; }
            if (awake[ClapC]  && clap.quiet())  { clap.reset();  awake[ClapC] = false; }
            if (awake[HatC]   && hat.quiet())   { hat.reset();   awake[HatC] = false; }
            if (awake[CrashC] && crash.quiet()) { crash.reset(); awake[CrashC] = false; }
            if (awake[RideC]  && ride.quiet())  { ride.reset();  awake[RideC] = false; }
        }

        // the mixer's coupling capacitor
        const float y = sum - dcX1 + dcCoef * dcY1;
        dcX1 = sum;
        dcY1 = y;

        out[n] = y * master.next(gainCoef);
    }
}

} // namespace tr909
