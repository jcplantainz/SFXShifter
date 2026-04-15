#include "PluginEditor.h"

SFXShifterEditor::SFXShifterEditor(SFXShifterAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(400, 440);

    resetButton.setButtonText("Reset");
    resetButton.onClick = [this]()
    {
        padX = 0.5f;
        padY = 0.5f;
        updateParameters();
        repaint();
    };
    addAndMakeVisible(resetButton);
}

SFXShifterEditor::~SFXShifterEditor() {}

float SFXShifterEditor::padXToSpeed(float x)
{
    if (x < 0.5f)
        return 0.5f + x;
    else
        return 1.0f + (x - 0.5f) * 2.0f;
}

void SFXShifterEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(30, 30, 30));

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("SFX Shifter", getLocalBounds().removeFromTop(40),
               juce::Justification::centred, true);

    // XY pad
    padArea = getLocalBounds().reduced(40).withTrimmedTop(20).withTrimmedBottom(60);
    g.setColour(juce::Colour(50, 50, 50));
    g.fillRect(padArea);

    // Grid lines
    g.setColour(juce::Colour(80, 80, 80));
    g.drawLine(padArea.getX(), padArea.getCentreY(),
               padArea.getRight(), padArea.getCentreY(), 1.0f);
    g.drawLine(padArea.getCentreX(), padArea.getY(),
               padArea.getCentreX(), padArea.getBottom(), 1.0f);

    // Axis labels
    g.setColour(juce::Colours::grey);
    g.setFont(12.0f);
    g.drawText("SLOWER", padArea.getX(), padArea.getBottom() + 5,
               60, 20, juce::Justification::left, true);
    g.drawText("FASTER", padArea.getRight() - 60, padArea.getBottom() + 5,
               60, 20, juce::Justification::right, true);
    g.drawText("PITCH+", padArea.getX() - 38, padArea.getY(),
               38, 20, juce::Justification::right, true);
    g.drawText("PITCH-", padArea.getX() - 38, padArea.getBottom() - 20,
               38, 20, juce::Justification::right, true);

    // Dot
    float dotX = padArea.getX() + padX * padArea.getWidth();
    float dotY = padArea.getY() + (1.0f - padY) * padArea.getHeight();
    g.setColour(juce::Colour(0, 200, 255));
    g.fillEllipse(dotX - 10, dotY - 10, 20, 20);
    g.setColour(juce::Colours::white);
    g.drawEllipse(dotX - 10, dotY - 10, 20, 20, 1.5f);

    // Value readout
    float speed = padXToSpeed(padX);
    float pitch = -12.0f + padY * 24.0f;
    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    g.drawText("Speed: " + juce::String(speed, 2) + "x  |  Pitch: "
               + juce::String(pitch, 1) + " st",
               juce::Rectangle<int>(0, padArea.getBottom() + 25, getWidth(), 20),
               juce::Justification::centred, true);
}

void SFXShifterEditor::resized()
{
    resetButton.setBounds(getWidth() / 2 - 40, getHeight() - 35, 80, 25);
}

void SFXShifterEditor::mouseDown(const juce::MouseEvent& e)
{
    mouseDrag(e);
}

void SFXShifterEditor::mouseDrag(const juce::MouseEvent& e)
{
    padArea = getLocalBounds().reduced(40).withTrimmedTop(20).withTrimmedBottom(60);

    if (!padArea.contains(e.getPosition()))
        return;

    padX = juce::jlimit(0.0f, 1.0f,
           (float)(e.x - padArea.getX()) / padArea.getWidth());
    padY = juce::jlimit(0.0f, 1.0f,
           1.0f - (float)(e.y - padArea.getY()) / padArea.getHeight());
    updateParameters();
    repaint();
}

void SFXShifterEditor::updateParameters()
{
    audioProcessor.speedValue = padXToSpeed(padX);
    audioProcessor.pitchValue = -12.0f + padY * 24.0f;
}