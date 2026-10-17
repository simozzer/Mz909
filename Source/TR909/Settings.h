// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

namespace tr909
{

//
// The front panel. Every knob is in panel units, 0 to 10, as printed round it on the
// original. The engine turns these into times, frequencies and levels itself, so the
// plugin layer never has to know the curves.
//
// The two hihats share one LEVEL, one DAC and one VCA after their ROMs; each
// has its own DECAY.
//
struct Settings
{
    float accent    = 5.0f;

    float bdLevel   = 8.0f;
    float bdTune    = 5.0f;
    float bdAttack  = 5.0f;
    float bdDecay   = 5.0f;

    float sdLevel   = 7.0f;
    float sdTune    = 5.0f;
    float sdTone    = 5.0f;
    float sdSnappy  = 5.0f;

    float ltLevel   = 7.0f;
    float ltTune    = 5.0f;
    float ltDecay   = 5.0f;
    float mtLevel   = 7.0f;
    float mtTune    = 5.0f;
    float mtDecay   = 5.0f;
    float htLevel   = 7.0f;
    float htTune    = 5.0f;
    float htDecay   = 5.0f;

    float rsLevel   = 6.0f;
    float cpLevel   = 7.0f;

    float hhLevel   = 6.0f;
    float chDecay   = 5.0f;
    float ohDecay   = 10.0f;

    float crLevel   = 6.0f;
    float crTune    = 5.0f;
    float rdLevel   = 6.0f;
    float rdTune    = 5.0f;

    float volume    = 7.0f;
};

} // namespace tr909
