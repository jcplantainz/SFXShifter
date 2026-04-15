#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class SFXShifterEditor : public juce::AudioProcessorEditor
{
public:
    SFXShifterEditor(SFXShifterAudioProcessor&);
    ~SFXShifterEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    SFXShifterAudioProcessor& audioProcessor;

    float padX { 0.5f };
    float padY { 0.5f };

    juce::Rectangle<int> padArea;
    juce::TextButton resetButton;

    float padXToSpeed(float x);
    void updateParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SFXShifterEditor)
};