#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>

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
    // FFT settings
    // fftOrder of 11 = 2048 point FFT
    // This gives good frequency resolution for pitch shifting
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    static constexpr int hopSize = fftSize / 4;   // 512 — overlap factor of 4

    juce::dsp::FFT fft;

    // Per-channel phase vocoder state
    struct ChannelState
    {
        // Input and output buffers
        std::vector<float> inputBuffer;
        std::vector<float> outputBuffer;
        int inputWritePos = 0;
        int outputReadPos = 0;

        // Phase vocoder analysis state
        std::vector<float> lastPhase;       // phase from previous frame
        std::vector<float> sumPhase;        // accumulated synthesis phase

        // FFT working buffers
        std::vector<float> fftWindow;       // Hann window
        std::vector<float> fftWorkspace;    // interleaved real/imag for JUCE FFT
        std::vector<float> magnitude;       // magnitude of each bin
        std::vector<float> frequency;       // true frequency of each bin
        std::vector<float> synthMagnitude;  // output magnitude
        std::vector<float> synthFrequency;  // output frequency
    };

    std::vector<ChannelState> channels;

    // Playback state
    bool isPlaying = false;
    double currentSampleRate = 44100.0;

    // Speed accumulator for sub-sample hop control
    double speedAccumulator = 0.0;

    // Smoothed parameters
    double smoothedSpeed = 1.0;
    double smoothedPitch = 0.0;

    void initChannelState(ChannelState& ch);
    void processChannel(ChannelState& ch, float* data, int numSamples,
                        double speed, double pitchShift);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SFXShifterAudioProcessor)
};