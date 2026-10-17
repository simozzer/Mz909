// Mz909 - an emulation of the TR-909 Rhythm Composer
// Created by Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

#include "Params.h"
#include "TR909/Engine.h"

class Mz909Processor final : public juce::AudioProcessor
{
public:
    Mz909Processor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState state;

    // The panel's pads play through this, alongside whatever MIDI the host sends.
    juce::MidiKeyboardState padState;
    void strike(int voice, bool accented);

    // For the pads' lights; only the engine's atomic accessors are safe here.
    const tr909::Engine& engineView() const { return engine; }

    // MIDI CC assignments, by control (its index in params::specs()). Each control answers
    // to one CC or none, and each CC drives one control at most. Safe from any thread.
    int  ccFor(int index) const          { return ccOf[(size_t) index].load(); }
    void learn(int index)                { learning = index; }   // -1 cancels
    int  learningIndex() const           { return learning.load(); }
    void forgetCc(int index)             { assignCc(index, -1); }
    void restoreDefaultCc(int index)     { assignCc(index, params::specs()[(size_t) index].cc); }
    void resetAllCcs();

    // bumped on every change, so the editor can tell when to repaint
    int  midiMapVersion() const          { return mapVersion.load(); }

private:
    void handle(const juce::MidiMessage& m);
    void assignCc(int index, int cc);

    params::Reader reader;
    tr909::Engine engine;

    static constexpr size_t maxControls = 64;
    std::vector<juce::RangedAudioParameter*> controls;   // by index, as params::specs()
    std::array<std::atomic<int>, maxControls> ccOf {};
    std::atomic<int> learning { -1 }, mapVersion { 0 };
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mz909Processor)
};
