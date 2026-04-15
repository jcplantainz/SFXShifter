#include "PluginProcessor.h"
#include "PluginEditor.h"

SFXShifterAudioProcessor::SFXShifterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

SFXShifterAudioProcessor::~SFXShifterAudioProcessor() {}

void SFXShifterAudioProcessor::resetStretcher()
{
    RubberBand::RubberBandStretcher::Options options =
        RubberBand::RubberBandStretcher::OptionProcessRealTime |
        RubberBand::RubberBandStretcher::OptionStretchPrecise |
        RubberBand::RubberBandStretcher::OptionTransientsCrisp;

    stretcher = std::make_unique<RubberBand::RubberBandStretcher>(
        (size_t)currentSampleRate,
        (size_t)getTotalNumInputChannels(),
        options
    );

    smoothedSpeed = (double)speedValue.load();
    smoothedPitch = (double)pitchValue.load();

    // Only set time ratio if slowing down
    // For speed-up we handle it manually
    double timeRatio = smoothedSpeed <= 1.0 ? (1.0 / smoothedSpeed) : 1.0;
    stretcher->setTimeRatio(timeRatio);
    stretcher->setPitchScale(std::pow(2.0, smoothedPitch / 12.0));

    startupSamplesRemaining = (int)stretcher->getLatency();
    skipAccumulator = 0.0;
}

void SFXShifterAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    double smoothingMs = 80.0;
    double samplesPerMs = sampleRate / 1000.0;
    speedSmoothing = 1.0 - (1.0 / (smoothingMs * samplesPerMs));
    pitchSmoothing = speedSmoothing;

    isPlaying = false;
    resetStretcher();
}

void SFXShifterAudioProcessor::releaseResources()
{
    stretcher.reset();
}

void SFXShifterAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (!stretcher) return;

    auto* playHead = getPlayHead();
    bool hostIsPlaying = false;

    if (playHead != nullptr)
    {
        if (auto position = playHead->getPosition())
            hostIsPlaying = position->getIsPlaying();
    }

    if (!hostIsPlaying && isPlaying)
    {
        resetStretcher();
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

    double targetSpeed = (double)speedValue.load();
    double targetPitch = (double)pitchValue.load();

    smoothedSpeed = smoothedSpeed * speedSmoothing + targetSpeed * (1.0 - speedSmoothing);
    smoothedPitch = smoothedPitch * pitchSmoothing + targetPitch * (1.0 - pitchSmoothing);

    smoothedSpeed = juce::jlimit(0.5, 2.0, smoothedSpeed);
    smoothedPitch = juce::jlimit(-12.0, 12.0, smoothedPitch);

    int numChannels = buffer.getNumChannels();
    int numSamples = buffer.getNumSamples();

    if (smoothedSpeed > 1.0)
    {
        // --- SPEED UP PATH: sample skipping + pitch shift ---

        // Apply pitch shift via Rubber Band with time ratio = 1.0
        stretcher->setTimeRatio(1.0);
        stretcher->setPitchScale(std::pow(2.0, smoothedPitch / 12.0));

        // Build a compressed input buffer by skipping samples
        // e.g. at 2x speed we keep every other sample
        int compressedSize = juce::jmax(1,
            (int)std::round(numSamples / smoothedSpeed));

        juce::AudioBuffer<float> compressedBuffer(numChannels, compressedSize);

        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = buffer.getReadPointer(c);
            float* dst = compressedBuffer.getWritePointer(c);

            for (int i = 0; i < compressedSize; ++i)
            {
                // Linear interpolation for smoother sample skipping
                double srcPos = (double)i * smoothedSpeed;
                int srcIdx = (int)srcPos;
                double frac = srcPos - srcIdx;

                int nextIdx = juce::jmin(srcIdx + 1, numSamples - 1);
                dst[i] = (float)((1.0 - frac) * src[srcIdx] + frac * src[nextIdx]);
            }
        }

        // Feed compressed buffer through Rubber Band for pitch only
        std::vector<const float*> inputPtrs(numChannels);
        for (int c = 0; c < numChannels; ++c)
            inputPtrs[c] = compressedBuffer.getReadPointer(c);

        stretcher->process(inputPtrs.data(), (size_t)compressedSize, false);

        // Retrieve output
        int available = (int)stretcher->available();

        if (startupSamplesRemaining > 0)
        {
            int toDiscard = std::min(startupSamplesRemaining, available);
            if (toDiscard > 0)
            {
                std::vector<std::vector<float>> drain(numChannels,
                    std::vector<float>(toDiscard, 0.0f));
                std::vector<float*> drainPtrs(numChannels);
                for (int c = 0; c < numChannels; ++c)
                    drainPtrs[c] = drain[c].data();
                stretcher->retrieve(drainPtrs.data(), (size_t)toDiscard);
                startupSamplesRemaining -= toDiscard;
            }
            buffer.clear();
            return;
        }

        if (available > 0)
        {
            int toRetrieve = std::min(available, numSamples);

            // Retrieve into a temp buffer then stretch back to full size
            juce::AudioBuffer<float> tempOut(numChannels, toRetrieve);
            std::vector<float*> outPtrs(numChannels);
            for (int c = 0; c < numChannels; ++c)
                outPtrs[c] = tempOut.getWritePointer(c);

            stretcher->retrieve(outPtrs.data(), (size_t)toRetrieve);

            // Copy to output, padding or truncating as needed
            for (int c = 0; c < numChannels; ++c)
            {
                const float* src = tempOut.getReadPointer(c);
                float* dst = buffer.getWritePointer(c);
                int toCopy = std::min(toRetrieve, numSamples);
                std::copy(src, src + toCopy, dst);
                if (toCopy < numSamples)
                    std::fill(dst + toCopy, dst + numSamples, 0.0f);
            }
        }
        else
        {
            buffer.clear();
        }
    }
    else
    {
        // --- SLOW DOWN PATH: full Rubber Band time stretching ---

        stretcher->setTimeRatio(1.0 / smoothedSpeed);
        stretcher->setPitchScale(std::pow(2.0, smoothedPitch / 12.0));

        std::vector<const float*> inputPtrs(numChannels);
        for (int c = 0; c < numChannels; ++c)
            inputPtrs[c] = buffer.getReadPointer(c);

        stretcher->process(inputPtrs.data(), (size_t)numSamples, false);

        int available = (int)stretcher->available();

        if (startupSamplesRemaining > 0)
        {
            int toDiscard = std::min(startupSamplesRemaining, available);
            if (toDiscard > 0)
            {
                std::vector<std::vector<float>> drain(numChannels,
                    std::vector<float>(toDiscard, 0.0f));
                std::vector<float*> drainPtrs(numChannels);
                for (int c = 0; c < numChannels; ++c)
                    drainPtrs[c] = drain[c].data();
                stretcher->retrieve(drainPtrs.data(), (size_t)toDiscard);
                startupSamplesRemaining -= toDiscard;
            }
            buffer.clear();
            return;
        }

        if (available > 0)
        {
            int toRetrieve = std::min(available, numSamples);
            std::vector<float*> outputPtrs(numChannels);
            for (int c = 0; c < numChannels; ++c)
                outputPtrs[c] = buffer.getWritePointer(c);

            stretcher->retrieve(outputPtrs.data(), (size_t)toRetrieve);

            if (toRetrieve < numSamples)
            {
                for (int c = 0; c < numChannels; ++c)
                    buffer.clear(c, toRetrieve, numSamples - toRetrieve);
            }
        }
        else
        {
            buffer.clear();
        }
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