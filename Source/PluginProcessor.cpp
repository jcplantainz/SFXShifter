#include "PluginProcessor.h"
#include "PluginEditor.h"

static constexpr double twoPi = juce::MathConstants<double>::twoPi;

SFXShifterAudioProcessor::SFXShifterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      fft(fftOrder)
{
}

SFXShifterAudioProcessor::~SFXShifterAudioProcessor() {}

void SFXShifterAudioProcessor::initChannelState(ChannelState& ch)
{
    ch.inputBuffer.assign(fftSize * 4, 0.0f);
    ch.outputBuffer.assign(fftSize * 4, 0.0f);
    ch.inputWritePos = 0;
    ch.outputReadPos = 0;

    ch.lastPhase.assign(fftSize / 2 + 1, 0.0f);
    ch.sumPhase.assign(fftSize / 2 + 1, 0.0f);

    ch.fftWindow.resize(fftSize);
    ch.fftWorkspace.assign(fftSize * 2, 0.0f);
    ch.magnitude.assign(fftSize / 2 + 1, 0.0f);
    ch.frequency.assign(fftSize / 2 + 1, 0.0f);
    ch.synthMagnitude.assign(fftSize / 2 + 1, 0.0f);
    ch.synthFrequency.assign(fftSize / 2 + 1, 0.0f);

    // Build Hann window
    // Hann window reduces spectral leakage between FFT frames
    for (int i = 0; i < fftSize; ++i)
        ch.fftWindow[i] = 0.5f * (1.0f - std::cos(twoPi * i / (fftSize - 1)));
}

void SFXShifterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    isPlaying = false;
    speedAccumulator = 0.0;
    smoothedSpeed = 1.0;
    smoothedPitch = 0.0;

    int numChannels = juce::jmax(getTotalNumInputChannels(),
                                  getTotalNumOutputChannels());
    channels.resize(numChannels);
    for (auto& ch : channels)
        initChannelState(ch);
}

void SFXShifterAudioProcessor::releaseResources()
{
    channels.clear();
}

void SFXShifterAudioProcessor::processChannel(
    ChannelState& ch,
    float* data,
    int numSamples,
    double speed,
    double pitchShift)
{
    // pitchShift is in semitones, convert to ratio
    double pitchRatio = std::pow(2.0, pitchShift / 12.0);

    // Expected phase advance per hop per bin
    // This is the "true frequency" of each bin at the nominal sample rate
    double phaseAdvancePerBin = twoPi * hopSize / fftSize;

    int bufSize = (int)ch.inputBuffer.size();

    for (int i = 0; i < numSamples; ++i)
    {
        // Write incoming sample into circular input buffer
        ch.inputBuffer[ch.inputWritePos] = data[i];
        ch.inputWritePos = (ch.inputWritePos + 1) % bufSize;

        // Accumulate speed — when we have enough samples for a hop, process a frame
        speedAccumulator += speed;

        while (speedAccumulator >= 1.0)
        {
            speedAccumulator -= 1.0;

            // --- ANALYSIS FRAME ---
            // Copy fftSize samples from input buffer into FFT workspace
            // Apply Hann window to reduce spectral leakage
            for (int k = 0; k < fftSize; ++k)
            {
                int readPos = (ch.inputWritePos - fftSize + k + bufSize) % bufSize;
                ch.fftWorkspace[k * 2]     = ch.inputBuffer[readPos] * ch.fftWindow[k];
                ch.fftWorkspace[k * 2 + 1] = 0.0f; // imaginary part = 0 for real input
            }

            // Forward FFT — converts time domain to frequency domain
            fft.performRealOnlyForwardTransform(ch.fftWorkspace.data());

            // --- PHASE VOCODER ANALYSIS ---
            // For each frequency bin, calculate the true frequency
            // by comparing the phase to what we'd expect
            int numBins = fftSize / 2 + 1;
            for (int k = 0; k < numBins; ++k)
            {
                float real = ch.fftWorkspace[k * 2];
                float imag = ch.fftWorkspace[k * 2 + 1];

                // Get magnitude and phase
                float mag = std::sqrt(real * real + imag * imag);
                float phase = std::atan2(imag, real);

                // Calculate phase difference from last frame
                double phaseDiff = phase - ch.lastPhase[k];
                ch.lastPhase[k] = phase;

                // Subtract expected phase advance
                double expectedAdvance = k * phaseAdvancePerBin;
                phaseDiff -= expectedAdvance;

                // Wrap phase difference to [-pi, pi]
                phaseDiff = phaseDiff - twoPi * std::round(phaseDiff / twoPi);

                // Calculate true frequency of this bin
                // (may differ from bin center due to pitch content)
                double trueFreq = (k + phaseDiff / phaseAdvancePerBin)
                                  * (twoPi * hopSize / fftSize);

                ch.magnitude[k] = mag;
                ch.frequency[k] = (float)trueFreq;
            }

            // --- PITCH SHIFTING ---
            // Redistribute energy across bins according to pitch ratio
            // This is where the actual pitch shift happens
            std::fill(ch.synthMagnitude.begin(), ch.synthMagnitude.end(), 0.0f);
            std::fill(ch.synthFrequency.begin(), ch.synthFrequency.end(), 0.0f);

            for (int k = 0; k < numBins; ++k)
            {
                // Calculate destination bin after pitch shift
                int destBin = (int)std::round(k * pitchRatio);

                if (destBin >= 0 && destBin < numBins)
                {
                    // Accumulate magnitude — multiple source bins
                    // can map to the same destination bin
                    if (ch.magnitude[k] > ch.synthMagnitude[destBin])
                    {
                        ch.synthMagnitude[destBin] = ch.magnitude[k];
                        ch.synthFrequency[destBin] = (float)(ch.frequency[k] * pitchRatio);
                    }
                }
            }

            // --- PHASE VOCODER SYNTHESIS ---
            // Accumulate synthesis phase using true frequencies
            for (int k = 0; k < numBins; ++k)
            {
                ch.sumPhase[k] += (float)ch.synthFrequency[k];

                float mag   = ch.synthMagnitude[k];
                float phase = ch.sumPhase[k];

                // Convert back to real/imaginary
                ch.fftWorkspace[k * 2]     = mag * std::cos(phase);
                ch.fftWorkspace[k * 2 + 1] = mag * std::sin(phase);
            }

            // Inverse FFT — converts back to time domain
            fft.performRealOnlyInverseTransform(ch.fftWorkspace.data());

            // --- OVERLAP-ADD ---
            // Add the windowed output frame to the output buffer
            // The overlap-add process reconstructs the continuous audio signal
            float windowSum = 0.0f;
            for (int k = 0; k < fftSize; ++k)
                windowSum += ch.fftWindow[k] * ch.fftWindow[k];
            float normalise = hopSize / (windowSum * fftSize);

            for (int k = 0; k < fftSize; ++k)
            {
                int writePos = (ch.outputReadPos + k) % bufSize;
                ch.outputBuffer[writePos] +=
                    ch.fftWorkspace[k * 2] * ch.fftWindow[k] * normalise;
            }
        }

        // Read output sample
        data[i] = ch.outputBuffer[ch.outputReadPos];
        ch.outputBuffer[ch.outputReadPos] = 0.0f;
        ch.outputReadPos = (ch.outputReadPos + 1) % bufSize;
    }
}

void SFXShifterAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Check playhead
    auto* playHead = getPlayHead();
    bool hostIsPlaying = false;

    if (playHead != nullptr)
        if (auto position = playHead->getPosition())
            hostIsPlaying = position->getIsPlaying();

    if (!hostIsPlaying && isPlaying)
    {
        // Reset state on stop
        for (auto& ch : channels)
            initChannelState(ch);
        speedAccumulator = 0.0;
        buffer.clear();
        isPlaying = false;
        return;
    }

    if (!hostIsPlaying)
    {
        buffer.clear();
        return;
    }

    isPlaying = true;

    // Smooth parameters to avoid abrupt changes
    double targetSpeed = (double)speedValue.load();
    double targetPitch = (double)pitchValue.load();
    smoothedSpeed = smoothedSpeed * 0.95 + targetSpeed * 0.05;
    smoothedPitch = smoothedPitch * 0.95 + targetPitch * 0.05;

    int numChannels = juce::jmin((int)channels.size(),
                                  buffer.getNumChannels());

    for (int c = 0; c < numChannels; ++c)
    {
        processChannel(channels[c],
                       buffer.getWritePointer(c),
                       buffer.getNumSamples(),
                       smoothedSpeed,
                       smoothedPitch);
    }
}

juce::AudioProcessorEditor* SFXShifterAudioProcessor::createEditor()
{
    return new SFXShifterEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SFXShifterAudioProcessor();
}