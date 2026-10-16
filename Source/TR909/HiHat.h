// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include "Rom.h"
#include "Trigger.h"

namespace tr909
{

//
// HI HAT: LEVEL, CLOSED DECAY, OPEN DECAY.
//
// Two 6-bit recordings, one for each hat, through one DAC and one VCA. Each hit starts its
// hat's recording from the top, and the VCA's envelope is let go through CLOSED DECAY or
// OPEN DECAY by which hat was struck. So a closed hat on top of an open one chokes it: the
// DAC switches to the closed recording and the VCA closes the fast way.
//
// The VCA has a threshold. Its envelope falls as an RC curve, but the gain is what is left
// above the threshold, so a hat holds, then falls faster and faster, and stops dead. That
// is the shape measured on a 909-style machine, for both hats - a closed hat dropping 41 dB
// in its last 50 ms, an open one gone at a stroke near half a second. The knobs' ranges put
// that machine's sounds at CLOSED DECAY 5 and OPEN DECAY 10.
//
class HiHat
{
public:
    void prepare(float sampleRate)
    {
        fs = sampleRate;
        dac.prepare(fs);
        env.prepare(fs);
        dcCoef = onePoleCoef(tauOf(30.0f), fs);
        set(5.0f, 10.0f);
        reset();
    }

    void set(float closedDecayPanel, float openDecayPanel)
    {
        closedTau = expLaw(closedDecayPanel / 10.0f, 0.04f, 0.36f);
        openTau = expLaw(openDecayPanel / 10.0f, 0.06f, 0.40f);
        env.setDecay(open ? openTau : closedTau);
    }

    void trigger(float height, bool asOpen)
    {
        open = asOpen;
        env.setDecay(open ? openTau : closedTau);
        pulse.fire(1.0f, fs);
        level = sampleAccent(height);
        dac.start(open ? openHatRom() : closedHatRom());
    }

    float process()
    {
        const float e = env.process(pulse.next());
        const float g = e > threshold ? (e - threshold) / (1.0f - threshold) : 0.0f;
        return dc.highPass(dac.next() * g * level, dcCoef) * gain;
    }

    bool quiet() const
    {
        return ! pulse.high() && (env.value() <= threshold || dac.finished()) && std::fabs(dc.z) < 1.0e-6f;
    }

    void reset()
    {
        dac.stop();
        env.reset();
        dc.reset();
    }

private:
    static constexpr float threshold = 0.3f;
    static constexpr float gain = 0.5f;

    float fs = 48000;
    TriggerPulse pulse;
    RomPlayer dac;
    Envelope env;
    OnePole dc;
    float dcCoef = 0, closedTau = 0.12f, openTau = 0.4f, level = 1;
    bool open = false;
};

} // namespace tr909
