// Mz909 - an emulation of the TR-909 Rhythm Composer
// Copyright (C) 2026 Simon Moscrop
//
// This program is free software: you can redistribute it and/or modify it under the terms of
// the GNU Affero General Public License as published by the Free Software Foundation, version 3.
// It is distributed WITHOUT ANY WARRANTY; see the LICENSE file for the full terms.

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Presets.h"

Mz909Processor::Mz909Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "Mz909", params::createLayout()),
      reader(state)
{
    jassert(params::specs().size() <= maxControls);

    for (const auto& p : params::specs())
    {
        jassert(params::learnable(p.cc));
        controls.push_back(state.getParameter(p.id));
    }

    resetAllCcs();
}

void Mz909Processor::assignCc(int index, int cc)
{
    if (index < 0 || index >= (int) controls.size()) return;

    // whoever had this CC gives it up
    if (cc >= 0)
        for (size_t i = 0; i < controls.size(); ++i)
            if ((int) i != index && ccOf[i].load() == cc)
                ccOf[i] = -1;

    ccOf[(size_t) index] = cc;
    ++mapVersion;
}

void Mz909Processor::resetAllCcs()
{
    for (size_t i = 0; i < controls.size(); ++i)
        ccOf[i] = params::specs()[i].cc;
    ++mapVersion;
}

void Mz909Processor::strike(int voice, bool accented)
{
    // a pad is a note like any other: the same path, the same sample-accurate timing
    const int note = tr909::voiceInfo(voice).note;
    const float velocity = (accented ? 127.0f : 100.0f) / 127.0f;
    padState.noteOn(10, note, velocity);
    padState.noteOff(10, note, 0.0f);
}

void Mz909Processor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    engine.setSettings(reader.read());
    padState.reset();
}

bool Mz909Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void Mz909Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    padState.processNextMidiBuffer(midi, 0, n, true);
    engine.setSettings(reader.read());

    buffer.clear();
    if (buffer.getNumChannels() == 0) return;

    // The 909 is a mono machine: render one channel, and split the block at each MIDI
    // event so a hit lands on its sample rather than at the top of the block.
    float* out = buffer.getWritePointer(0);
    int pos = 0;

    for (const auto meta : midi)
    {
        const int at = juce::jlimit(0, n, meta.samplePosition);
        if (at > pos)
        {
            engine.render(out + pos, at - pos);
            pos = at;
        }
        handle(meta.getMessage());
    }

    if (pos < n) engine.render(out + pos, n - pos);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, n);
}

void Mz909Processor::handle(const juce::MidiMessage& m)
{
    // Every channel plays: a drum part from the host is often on 10, a pad controller
    // on whatever it likes.
    if (m.isNoteOn())
        engine.noteOn(m.getNoteNumber(), m.getVelocity());
    else if (m.isAllSoundOff())
        engine.allSoundOff();
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();

        // MIDI learn: the first learnable CC to arrive is taken, and does nothing else.
        int waiting = learning.load();
        if (waiting >= 0 && params::learnable(cc) && learning.compare_exchange_strong(waiting, -1))
        {
            assignCc(waiting, cc);
            return;
        }

        // A mapped CC moves the parameter itself - so the panel follows, the host can record
        // it, and the project saves it - and takes effect from this sample, not the next block.
        for (size_t i = 0; i < controls.size(); ++i)
        {
            if (ccOf[i].load(std::memory_order_relaxed) == cc)
            {
                controls[i]->setValueNotifyingHost((float) m.getControllerValue() / 127.0f);
                engine.setSettings(reader.read());
                break;
            }
        }
    }
}

juce::AudioProcessorEditor* Mz909Processor::createEditor()
{
    return new Mz909Editor(*this);
}

int Mz909Processor::getNumPrograms()
{
    return (int) presets::all().size();
}

void Mz909Processor::setCurrentProgram(int index)
{
    if (index < 0 || index >= getNumPrograms()) return;
    currentProgram = index;
    presets::apply(presets::all()[(size_t) index], state);
}

const juce::String Mz909Processor::getProgramName(int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    return presets::all()[(size_t) index].name;
}

void Mz909Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto copy = state.copyState();
    copy.setProperty("program", currentProgram, nullptr);

    // The CC assignments, as id=cc (-1 for none), so a control added or removed later
    // cannot shift anyone else's.
    juce::StringArray map;
    for (size_t i = 0; i < controls.size(); ++i)
        map.add(juce::String(params::specs()[i].id) + "=" + juce::String(ccOf[i].load()));
    copy.setProperty("midiMap", map.joinIntoString(","), nullptr);

    if (auto xml = copy.createXml())
        copyXmlToBinary(*xml, destData);
}

void Mz909Processor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml(*xml);
            currentProgram = tree.getProperty("program", 0);

            // CC assignments: the defaults, then whatever the project changed.
            learning = -1;
            resetAllCcs();
            juce::StringArray map;
            map.addTokens(tree.getProperty("midiMap").toString(), ",", {});
            for (const auto& entry : map)
            {
                const int index = params::indexOf(entry.upToFirstOccurrenceOf("=", false, false));
                const int cc = entry.fromFirstOccurrenceOf("=", false, false).getIntValue();
                if (index >= 0 && (cc == -1 || params::learnable(cc)))
                    ccOf[(size_t) index] = cc;
            }
            ++mapVersion;

            state.replaceState(tree);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Mz909Processor();
}
