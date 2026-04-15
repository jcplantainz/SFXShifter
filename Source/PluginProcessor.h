#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "rubberband/RubberBandStretcher.h"

class SFXShifterAudioProcessor : public juce::AudioProcessor
{
public:
    SFXShifterAudioProcessor();
    ~SFXShifterAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SFX Shifter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    std::atomic<float> speedValue { 1.0f };
    std::atomic<float> pitchValue { 0.0f };

private:
    std::unique_ptr<RubberBand::RubberBandStretcher> stretcher;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool isPlaying = false;
    int startupSamplesRemaining = 0;

    double smoothedSpeed = 1.0;
    double smoothedPitch = 0.0;
    double speedSmoothing = 0.95;
    double pitchSmoothing = 0.95;
    double skipAccumulator = 0.0;

    void resetStretcher();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SFXShifterAudioProcessor)
};