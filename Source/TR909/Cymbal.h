// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
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
// CRASH CYMBAL and RIDE CYMBAL: LEVEL, TUNE each.
//
// Each its own 6-bit recording, played to its end: the cymbal's decay is in the ROM, not
// a VCA, so there is no DECAY. TUNE is the ROM's clock, a little over half an octave
// either way - faster is higher and shorter, slower lower and longer, as a sample played
// at another speed is. An accent is a louder hit through the VCA after the DAC.
//
// The structure is the 909's. The TUNE range is reasoned from the sound.
//
class Cymbal
{
public:
    explicit Cymbal(const Rom& (*whichRom)()) : romOf(whichRom) {}

    void prepare(float sampleRate)
    {
        fs = sampleRate;
        dac.prepare(fs);
        dcCoef = onePoleCoef(tauOf(30.0f), fs);
        set(5.0f);
        reset();
    }

    void set(float tunePanel) { dac.setSpeed(expLaw(tunePanel / 10.0f, 0.68f, 1.47f)); }

    void trigger(float height)
    {
        level = sampleAccent(height);
        dac.start(romOf());
    }

    float process() { return dc.highPass(dac.next() * level, dcCoef) * gain; }

    bool quiet() const { return dac.finished() && std::fabs(dc.z) < 1.0e-6f; }

    void reset()
    {
        dac.stop();
        dc.reset();
    }

private:
    static constexpr float gain = 0.5f;

    const Rom& (*romOf)();
    float fs = 48000;
    RomPlayer dac;
    OnePole dc;
    float dcCoef = 0, level = 1;
};

} // namespace tr909
