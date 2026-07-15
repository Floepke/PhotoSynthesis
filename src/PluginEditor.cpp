#include "PluginEditor.h"
#include "Version.h"

#include <cmath>

namespace
{
constexpr int kNumLfos = 8;
constexpr std::array<const char*, 9> kSyncDivisionNames{ "1/1", "1/2", "1/4", "1/8", "1/16", "1/2T", "1/4T", "1/8T", "1/16T" };
constexpr std::array<double, 9> kSyncDivisionBeatsPerCycle{ 4.0, 2.0, 1.0, 0.5, 0.25, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
const std::array<juce::Colour, kNumLfos> kLfoTabColours{
    juce::Colour::fromRGB(168, 92, 88),
    juce::Colour::fromRGB(174, 120, 86),
    juce::Colour::fromRGB(166, 142, 88),
    juce::Colour::fromRGB(112, 152, 96),
    juce::Colour::fromRGB(86, 148, 144),
    juce::Colour::fromRGB(92, 130, 176),
    juce::Colour::fromRGB(112, 106, 178),
    juce::Colour::fromRGB(154, 98, 170)
};

juce::Colour getLfoTabColourForIndex(int tabIndex)
{
    const auto index = static_cast<size_t>(juce::jlimit(0, kNumLfos - 1, tabIndex));
    return kLfoTabColours[index];
}

juce::Colour getLfoContentBackgroundForIndex(int tabIndex)
{
    return getLfoTabColourForIndex(tabIndex)
        .withMultipliedSaturation(0.48f)
        .withMultipliedBrightness(0.30f)
        .withAlpha(0.95f);
}

constexpr std::array<const char*, 38> kModTargetNames{
    "None",
    "Attack", "Decay", "Sustain", "Release", "Gain", "Note Drift", "Drift Freq",
    "Line X", "Line Y", "Line Length", "Line Angle",
    "Oval X1", "Oval Y1", "Oval X2", "Oval Y2",
    "Rect X", "Rect Y", "Rect Width", "Rect Height",
    "Tri X1", "Tri Y1", "Tri X2", "Tri Y2", "Tri X3", "Tri Y3",
    "Prop X", "Prop Y", "Prop Size", "Prop Speed",
    "R->L", "G->L", "B->L", "A->L", "R->R", "G->R", "B->R", "A->R"
};

float clampParameterValue(const juce::String& paramId, float value)
{
    if (paramId == "scanAngle")
        return juce::jlimit(-180.0f, 180.0f, value);

    if (paramId == "scanLength" || paramId == "rectWidth" || paramId == "rectHeight" || paramId == "propSize")
        return juce::jlimit(0.05f, 1.5f, value);

    if (paramId == "propSpeed")
        return juce::jlimit(0.0f, 10.0f, value);

    return juce::jlimit(0.0f, 1.0f, value);
}

double beatsPerCycleFromSyncDivision(int division)
{
    const auto clamped = static_cast<size_t>(juce::jlimit(0, static_cast<int>(kSyncDivisionBeatsPerCycle.size()) - 1, division));
    return kSyncDivisionBeatsPerCycle[clamped];
}

juce::String syncDivisionTextFromIndex(int division)
{
    const auto clamped = static_cast<size_t>(juce::jlimit(0, static_cast<int>(kSyncDivisionNames.size()) - 1, division));
    return kSyncDivisionNames[clamped];
}
}

void PictureWaveSynthAudioProcessorEditor::ModulationSlider::setModulationVisual(float newAmount)
{
    const auto clampedAmount = juce::jlimit(-1.0f, 1.0f, newAmount);
    if (std::abs(modulationAmount - clampedAmount) < 0.001f)
    {
        return;
    }

    modulationAmount = clampedAmount;
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ModulationSlider::setEffectiveNormalisedValue(float newValue)
{
    const auto clampedValue = juce::jlimit(0.0f, 1.0f, newValue);
    if (std::abs(effectiveNormalisedValue - clampedValue) < 0.001f)
    {
        return;
    }

    effectiveNormalisedValue = clampedValue;
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ModulationSlider::paintOverChildren(juce::Graphics& g)
{
    const auto amount = juce::jlimit(-1.0f, 1.0f, modulationAmount);
    const auto magnitude = std::abs(amount);
    const auto positiveColour = juce::Colour::fromRGB(89, 208, 149);
    const auto negativeColour = juce::Colour::fromRGB(255, 145, 92);
    const auto neutralColour = juce::Colour::fromRGB(122, 190, 255);
    const auto accentBase = magnitude < 0.04f ? neutralColour : (amount >= 0.0f ? positiveColour : negativeColour);
    const auto accent = accentBase.withAlpha(0.55f + 0.35f * magnitude);
    const auto sliderLayout = getLookAndFeel().getSliderLayout(*this);

    if (isRotary())
    {
        auto area = sliderLayout.sliderBounds.toFloat().reduced(3.0f);
        const auto radius = juce::jmax(6.0f, juce::jmin(area.getWidth(), area.getHeight()) * 0.5f - 1.5f);
        const auto thickness = juce::jmax(2.0f, radius * 0.11f);
        const auto centreX = area.getCentreX();
        const auto centreY = area.getCentreY();
        const auto startAngle = juce::MathConstants<float>::pi * 1.2f;
        const auto endAngle = juce::MathConstants<float>::pi * 2.8f;
        const auto totalAngle = endAngle - startAngle;
        const auto indicatorAngle = startAngle + totalAngle * effectiveNormalisedValue;
        const auto indicatorSweep = juce::jmax(0.12f, totalAngle * 0.075f);
        const auto arcStart = juce::jlimit(startAngle, endAngle, indicatorAngle - indicatorSweep * 0.5f);
        const auto arcEnd = juce::jlimit(startAngle, endAngle, indicatorAngle + indicatorSweep * 0.5f);

        juce::Path baseArc;
        baseArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.strokePath(baseArc, juce::PathStrokeType(thickness));

        juce::Path activeArc;
        activeArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, arcStart, arcEnd, true);
        g.setColour(accent);
        g.strokePath(activeArc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }

    auto area = sliderLayout.sliderBounds.toFloat().reduced(3.0f);
    if (getSliderStyle() == juce::Slider::LinearVertical)
    {
        const auto verticalAccent = findColour(juce::Slider::thumbColourId).withAlpha(0.95f);
        const auto markerY = area.getBottom() - area.getHeight() * effectiveNormalisedValue;
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(area.getRight() - 4.0f, area.getY(), 4.0f, area.getHeight(), 2.0f);

        if (mappingOverlayEnabled)
        {
            const auto centerY = area.getBottom() - area.getHeight() * 0.5f;
            const auto fillTop = juce::jmin(centerY, markerY);
            const auto fillHeight = juce::jmax(2.0f, std::abs(markerY - centerY));
            g.fillRoundedRectangle(area.getX() + 1.0f, centerY - 1.5f, area.getWidth() - 2.0f, 3.0f, 1.5f);
            g.setColour(verticalAccent);
            g.fillRoundedRectangle(area.getX() + 1.0f, fillTop - 1.5f, area.getWidth() - 2.0f, fillHeight + 3.0f, 2.0f);
        }
        else
        {
            g.setColour(verticalAccent);
            g.fillRoundedRectangle(area.getX() + 1.0f, markerY - 1.5f, area.getWidth() - 2.0f, 3.0f, 1.5f);
        }
    }
    else
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(area.getX(), area.getBottom() - 4.0f, area.getWidth(), 4.0f, 2.0f);

        const auto markerX = area.getX() + area.getWidth() * effectiveNormalisedValue;
        g.setColour(accent);
        g.fillRoundedRectangle(markerX - 2.5f, area.getY() + 1.0f, 5.0f, area.getHeight() - 2.0f, 2.0f);
    }
}

void PictureWaveSynthAudioProcessorEditor::ModulationSlider::mouseWheelMove(const juce::MouseEvent& event,
                                                                             const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(event);

    const auto axis = std::abs(wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX;
    if (axis == 0.0f)
    {
        return;
    }

    auto step = getInterval();
    if (step <= 0.0)
    {
        step = 1.0;
    }

    const auto direction = axis > 0.0f ? 1.0 : -1.0;
    const auto nextValue = juce::jlimit(getMinimum(), getMaximum(), getValue() + direction * step);
    setValue(nextValue, juce::sendNotificationSync);
}

void PictureWaveSynthAudioProcessorEditor::LfoVisualizer::setVisualState(float newPhase, float newDepth, int newWave, juce::Colour newAccent)
{
    const auto clampedPhase = newPhase - std::floor(newPhase);
    const auto clampedDepth = juce::jlimit(0.0f, 1.0f, newDepth);
    const auto clampedWave = juce::jlimit(1, 7, newWave);

    if (std::abs(phase - clampedPhase) < 0.0005f
        && std::abs(depth - clampedDepth) < 0.0005f
        && wave == clampedWave
        && accent == newAccent)
    {
        return;
    }

    phase = clampedPhase;
    depth = clampedDepth;
    wave = clampedWave;
    accent = newAccent;
    repaint();
}

float PictureWaveSynthAudioProcessorEditor::LfoVisualizer::hashNoise(int x)
{
    const auto n = static_cast<uint32_t>(x) * 1664525u + 1013904223u;
    return static_cast<float>((n >> 8) & 0xffffu) / 32767.5f - 1.0f;
}

float PictureWaveSynthAudioProcessorEditor::LfoVisualizer::sampleWave(float phaseValue, int waveType)
{
    const auto t = phaseValue - std::floor(phaseValue);
    switch (waveType)
    {
        case 2:
        {
            return 1.0f - 4.0f * std::abs(t - 0.5f);
        }
        case 3:
        {
            return 2.0f * t - 1.0f;
        }
        case 4:
        {
            return t < 0.5f ? 1.0f : -1.0f;
        }
        case 5:
        {
            const int step = static_cast<int>(std::floor(t * 10.0f));
            return hashNoise(step);
        }
        case 6:
        {
            const auto segment = t * 6.0f;
            const int left = static_cast<int>(std::floor(segment));
            const float frac = segment - static_cast<float>(left);
            const float a = hashNoise(left);
            const float b = hashNoise(left + 1);
            return a + (b - a) * frac;
        }
        case 7:
        {
            const auto segment = t * 4.0f;
            const int left = static_cast<int>(std::floor(segment));
            const float frac = segment - static_cast<float>(left);
            const float smooth = frac * frac * (3.0f - 2.0f * frac);
            const float a = hashNoise(left);
            const float b = hashNoise(left + 1);
            return a + (b - a) * smooth;
        }
        case 1:
        default:
        {
            return std::sin(t * juce::MathConstants<float>::twoPi);
        }
    }
}

void PictureWaveSynthAudioProcessorEditor::LfoVisualizer::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(2.0f);
    if (area.getWidth() < 12.0f || area.getHeight() < 12.0f)
    {
        return;
    }

    g.setColour(juce::Colour::fromRGB(20, 23, 29).withAlpha(0.62f));
    g.fillRoundedRectangle(area, 6.0f);

    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.drawRoundedRectangle(area, 6.0f, 1.0f);

    const auto centreY = area.getCentreY();
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.drawLine(area.getX() + 4.0f, centreY, area.getRight() - 4.0f, centreY, 1.0f);

    const auto amp = (area.getHeight() * 0.38f) * juce::jmax(0.08f, depth);
    juce::Path wavePath;
    constexpr int steps = 96;
    for (int i = 0; i <= steps; ++i)
    {
        const auto xNorm = static_cast<float>(i) / static_cast<float>(steps);
        const auto x = area.getX() + xNorm * area.getWidth();
        const auto y = centreY - sampleWave(phase + xNorm, wave) * amp;
        if (i == 0)
        {
            wavePath.startNewSubPath(x, y);
        }
        else
        {
            wavePath.lineTo(x, y);
        }
    }

    const auto lineColour = accent.withMultipliedSaturation(0.8f).withMultipliedBrightness(1.05f).withAlpha(0.95f);
    g.setColour(lineColour.withAlpha(0.30f));
    g.strokePath(wavePath, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(lineColour);
    g.strokePath(wavePath, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto headY = centreY - sampleWave(phase, wave) * amp;
    g.setColour(lineColour.withAlpha(0.85f));
    g.fillEllipse(area.getX() + 2.0f, headY - 2.0f, 4.0f, 4.0f);
}

void PictureWaveSynthAudioProcessorEditor::ResettableComboBox::setResetSelectedId(int newSelectedId)
{
    resetSelectedId = newSelectedId;
}

void PictureWaveSynthAudioProcessorEditor::ResettableComboBox::mouseDoubleClick(const juce::MouseEvent& event)
{
    juce::ComboBox::mouseDoubleClick(event);
    if (resetSelectedId > 0)
    {
        setSelectedId(resetSelectedId, juce::sendNotificationSync);
    }
}

void PictureWaveSynthAudioProcessorEditor::ResettableComboBox::mouseWheelMove(const juce::MouseEvent& event,
                                                                               const juce::MouseWheelDetails& wheel)
{
    juce::ignoreUnused(event);

    const auto axis = std::abs(wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX;
    if (axis == 0.0f || getNumItems() <= 0)
    {
        return;
    }

    auto currentIndex = getSelectedItemIndex();
    if (currentIndex < 0)
    {
        currentIndex = 0;
    }

    const auto direction = axis < 0.0f ? 1 : -1;
    const auto nextIndex = juce::jlimit(0, getNumItems() - 1, currentIndex + direction);
    if (nextIndex != currentIndex)
    {
        setSelectedItemIndex(nextIndex, juce::sendNotificationSync);
    }
}

void PictureWaveSynthAudioProcessorEditor::ResettableToggleButton::setResetState(bool newResetState)
{
    resetState = newResetState;
}

void PictureWaveSynthAudioProcessorEditor::ResettableToggleButton::mouseDoubleClick(const juce::MouseEvent& event)
{
    juce::ToggleButton::mouseDoubleClick(event);
    setToggleState(resetState, juce::sendNotificationSync);
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::setImage(juce::Image newImage)
{
    image = std::move(newImage);
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::setParameterChangeCallback(ParameterChangeCallback callback)
{
    parameterChangeCallback = std::move(callback);
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::setScanner(
    int mode,
    float newLineX,
    float newLineY,
    float newLineLength,
    float newLineAngleDegrees,
    float newOvalX1,
    float newOvalY1,
    float newOvalX2,
    float newOvalY2,
    float newRectX,
    float newRectY,
    float newRectWidth,
    float newRectHeight,
    float newTriX1,
    float newTriY1,
    float newTriX2,
    float newTriY2,
    float newTriX3,
    float newTriY3,
    float newPropX,
    float newPropY,
    float newPropSize,
    double newPropPhase)
{
    scannerMode = mode;
    lineX = newLineX;
    lineY = newLineY;
    lineLength = newLineLength;
    lineAngle = newLineAngleDegrees;
    ovalX1 = newOvalX1;
    ovalY1 = newOvalY1;
    ovalX2 = newOvalX2;
    ovalY2 = newOvalY2;
    rectX = newRectX;
    rectY = newRectY;
    rectWidth = newRectWidth;
    rectHeight = newRectHeight;
    triX1 = newTriX1;
    triY1 = newTriY1;
    triX2 = newTriX2;
    triY2 = newTriY2;
    triX3 = newTriX3;
    triY3 = newTriY3;
    propX = newPropX;
    propY = newPropY;
    propSize = newPropSize;
    propPhase = newPropPhase;
    repaint();
}

juce::Rectangle<float> PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::getImageDrawArea() const
{
    auto content = getLocalBounds().toFloat().reduced(8.0f);
    if (!image.isValid())
    {
        return content;
    }

    const auto imageAspect = static_cast<float>(image.getWidth()) / static_cast<float>(image.getHeight());
    const auto contentAspect = content.getWidth() / content.getHeight();

    if (imageAspect > contentAspect)
    {
        const auto h = content.getWidth() / imageAspect;
        return { content.getX(), content.getCentreY() - h * 0.5f, content.getWidth(), h };
    }

    const auto w = content.getHeight() * imageAspect;
    return { content.getCentreX() - w * 0.5f, content.getY(), w, content.getHeight() };
}

juce::Point<float> PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::normalisedToPoint(float u, float v) const
{
    const auto drawArea = getImageDrawArea();
    return {
        drawArea.getX() + juce::jlimit(0.0f, 1.0f, u) * drawArea.getWidth(),
        drawArea.getY() + juce::jlimit(0.0f, 1.0f, v) * drawArea.getHeight()
    };
}

std::array<float, 2> PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::pointToNormalised(juce::Point<float> p) const
{
    const auto drawArea = getImageDrawArea();
    if (drawArea.getWidth() <= 1.0f || drawArea.getHeight() <= 1.0f)
    {
        return { 0.5f, 0.5f };
    }

    const auto u = (p.x - drawArea.getX()) / drawArea.getWidth();
    const auto v = (p.y - drawArea.getY()) / drawArea.getHeight();
    return { juce::jlimit(0.0f, 1.0f, u), juce::jlimit(0.0f, 1.0f, v) };
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::emitParameter(const juce::String& paramId, float value)
{
    if (parameterChangeCallback)
    {
        parameterChangeCallback(paramId, clampParameterValue(paramId, value));
    }
}

PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::DragHandle
PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::hitTestHandle(juce::Point<float> p) const
{
    const auto near = [p](juce::Point<float> hp)
    {
        return p.getDistanceFrom(hp) <= 10.0f;
    };

    if (scannerMode == 1)
    {
        const auto center = normalisedToPoint(0.5f * (ovalX1 + ovalX2), 0.5f * (ovalY1 + ovalY2));
        if (near(center)) return DragHandle::ovalCenter;
        if (near(normalisedToPoint(ovalX1, ovalY1))) return DragHandle::ovalP1;
        if (near(normalisedToPoint(ovalX2, ovalY2))) return DragHandle::ovalP2;
        return DragHandle::none;
    }

    if (scannerMode == 2)
    {
        const auto center = normalisedToPoint(rectX, rectY);
        const auto tl = normalisedToPoint(rectX - rectWidth * 0.5f, rectY - rectHeight * 0.5f);
        const auto br = normalisedToPoint(rectX + rectWidth * 0.5f, rectY + rectHeight * 0.5f);
        if (near(center)) return DragHandle::rectCenter;
        if (near(tl)) return DragHandle::rectTL;
        if (near(br)) return DragHandle::rectBR;
        return DragHandle::none;
    }

    if (scannerMode == 3)
    {
        if (near(normalisedToPoint(triX1, triY1))) return DragHandle::triP1;
        if (near(normalisedToPoint(triX2, triY2))) return DragHandle::triP2;
        if (near(normalisedToPoint(triX3, triY3))) return DragHandle::triP3;
        return DragHandle::none;
    }

    if (scannerMode == 4)
    {
        const auto center = normalisedToPoint(propX, propY);
        const auto angleRadians = static_cast<float>(propPhase);
        const auto dx = std::cos(angleRadians) * propSize * 0.5f;
        const auto dy = std::sin(angleRadians) * propSize * 0.5f;
        const auto a = normalisedToPoint(propX - dx, propY - dy);
        const auto b = normalisedToPoint(propX + dx, propY + dy);
        if (near(center)) return DragHandle::lineCenter;
        if (near(a)) return DragHandle::lineA;
        if (near(b)) return DragHandle::lineB;
        return DragHandle::none;
    }

    const auto center = normalisedToPoint(lineX, lineY);
    const auto angleRadians = juce::degreesToRadians(lineAngle);
    const auto dx = std::cos(angleRadians) * lineLength * 0.5f;
    const auto dy = std::sin(angleRadians) * lineLength * 0.5f;
    const auto a = normalisedToPoint(lineX - static_cast<float>(dx), lineY - static_cast<float>(dy));
    const auto b = normalisedToPoint(lineX + static_cast<float>(dx), lineY + static_cast<float>(dy));
    if (near(center)) return DragHandle::lineCenter;
    if (near(a)) return DragHandle::lineA;
    if (near(b)) return DragHandle::lineB;
    return DragHandle::none;
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::dragLineHandle(DragHandle handle, juce::Point<float> p)
{
    auto uv = pointToNormalised(p);

    const auto angleRadians = juce::degreesToRadians(lineAngle);
    const auto dx = std::cos(angleRadians) * lineLength * 0.5f;
    const auto dy = std::sin(angleRadians) * lineLength * 0.5f;
    auto a = std::array<float, 2>{ lineX - static_cast<float>(dx), lineY - static_cast<float>(dy) };
    auto b = std::array<float, 2>{ lineX + static_cast<float>(dx), lineY + static_cast<float>(dy) };

    if (handle == DragHandle::lineCenter)
    {
        lineX = uv[0];
        lineY = uv[1];
        emitParameter("scanX", lineX);
        emitParameter("scanY", lineY);
        repaint();
        return;
    }

    if (handle == DragHandle::lineA)
    {
        a = uv;
    }
    else if (handle == DragHandle::lineB)
    {
        b = uv;
    }

    lineX = 0.5f * (a[0] + b[0]);
    lineY = 0.5f * (a[1] + b[1]);
    const auto ddx = b[0] - a[0];
    const auto ddy = b[1] - a[1];
    lineLength = juce::jlimit(0.05f, 1.5f, std::sqrt(ddx * ddx + ddy * ddy));
    lineAngle = juce::radiansToDegrees(std::atan2(ddy, ddx));

    emitParameter("scanX", lineX);
    emitParameter("scanY", lineY);
    emitParameter("scanLength", lineLength);
    emitParameter("scanAngle", lineAngle);
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::dragPropHandle(DragHandle handle, juce::Point<float> p)
{
    const auto uv = pointToNormalised(p);

    const auto angleRadians = static_cast<float>(propPhase);
    const auto dx = std::cos(angleRadians) * propSize * 0.5f;
    const auto dy = std::sin(angleRadians) * propSize * 0.5f;
    auto a = std::array<float, 2>{ propX - dx, propY - dy };
    auto b = std::array<float, 2>{ propX + dx, propY + dy };

    if (handle == DragHandle::lineCenter)
    {
        propX = uv[0];
        propY = uv[1];
        emitParameter("propX", propX);
        emitParameter("propY", propY);
        repaint();
        return;
    }

    if (handle == DragHandle::lineA)
    {
        a = uv;
    }
    else if (handle == DragHandle::lineB)
    {
        b = uv;
    }

    propX = 0.5f * (a[0] + b[0]);
    propY = 0.5f * (a[1] + b[1]);
    const auto ddx = b[0] - a[0];
    const auto ddy = b[1] - a[1];
    propSize = juce::jlimit(0.05f, 1.5f, std::sqrt(ddx * ddx + ddy * ddy));

    emitParameter("propX", propX);
    emitParameter("propY", propY);
    emitParameter("propSize", propSize);
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::dragOvalHandle(DragHandle handle, juce::Point<float> p)
{
    const auto uv = pointToNormalised(p);
    if (handle == DragHandle::ovalCenter)
    {
        const auto halfWidth = 0.5f * (ovalX2 - ovalX1);
        const auto halfHeight = 0.5f * (ovalY2 - ovalY1);

        ovalX1 = juce::jlimit(0.0f, 1.0f, uv[0] - halfWidth);
        ovalY1 = juce::jlimit(0.0f, 1.0f, uv[1] - halfHeight);
        ovalX2 = juce::jlimit(0.0f, 1.0f, uv[0] + halfWidth);
        ovalY2 = juce::jlimit(0.0f, 1.0f, uv[1] + halfHeight);

        emitParameter("ovalX1", ovalX1);
        emitParameter("ovalY1", ovalY1);
        emitParameter("ovalX2", ovalX2);
        emitParameter("ovalY2", ovalY2);
    }
    else if (handle == DragHandle::ovalP1)
    {
        ovalX1 = uv[0];
        ovalY1 = uv[1];
        emitParameter("ovalX1", ovalX1);
        emitParameter("ovalY1", ovalY1);
    }
    else if (handle == DragHandle::ovalP2)
    {
        ovalX2 = uv[0];
        ovalY2 = uv[1];
        emitParameter("ovalX2", ovalX2);
        emitParameter("ovalY2", ovalY2);
    }
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::dragRectHandle(DragHandle handle, juce::Point<float> p)
{
    const auto uv = pointToNormalised(p);
    if (handle == DragHandle::rectCenter)
    {
        rectX = uv[0];
        rectY = uv[1];
        emitParameter("rectX", rectX);
        emitParameter("rectY", rectY);
        repaint();
        return;
    }

    auto tl = std::array<float, 2>{ rectX - rectWidth * 0.5f, rectY - rectHeight * 0.5f };
    auto br = std::array<float, 2>{ rectX + rectWidth * 0.5f, rectY + rectHeight * 0.5f };
    if (handle == DragHandle::rectTL)
    {
        tl = uv;
    }
    else if (handle == DragHandle::rectBR)
    {
        br = uv;
    }

    rectX = 0.5f * (tl[0] + br[0]);
    rectY = 0.5f * (tl[1] + br[1]);
    rectWidth = juce::jlimit(0.05f, 1.5f, std::abs(br[0] - tl[0]));
    rectHeight = juce::jlimit(0.05f, 1.5f, std::abs(br[1] - tl[1]));

    emitParameter("rectX", rectX);
    emitParameter("rectY", rectY);
    emitParameter("rectWidth", rectWidth);
    emitParameter("rectHeight", rectHeight);
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::dragTriHandle(DragHandle handle, juce::Point<float> p)
{
    const auto uv = pointToNormalised(p);
    if (handle == DragHandle::triP1)
    {
        triX1 = uv[0];
        triY1 = uv[1];
        emitParameter("triX1", triX1);
        emitParameter("triY1", triY1);
    }
    else if (handle == DragHandle::triP2)
    {
        triX2 = uv[0];
        triY2 = uv[1];
        emitParameter("triX2", triX2);
        emitParameter("triY2", triY2);
    }
    else if (handle == DragHandle::triP3)
    {
        triX3 = uv[0];
        triY3 = uv[1];
        emitParameter("triX3", triX3);
        emitParameter("triY3", triY3);
    }
    repaint();
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::mouseDown(const juce::MouseEvent& event)
{
    activeHandle = hitTestHandle(event.position);
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (activeHandle == DragHandle::none)
    {
        return;
    }

    if (scannerMode == 1)
    {
        dragOvalHandle(activeHandle, event.position);
        return;
    }
    if (scannerMode == 2)
    {
        dragRectHandle(activeHandle, event.position);
        return;
    }
    if (scannerMode == 3)
    {
        dragTriHandle(activeHandle, event.position);
        return;
    }

    if (scannerMode == 4)
    {
        dragPropHandle(activeHandle, event.position);
        return;
    }

    dragLineHandle(activeHandle, event.position);
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    activeHandle = DragHandle::none;
}

void PictureWaveSynthAudioProcessorEditor::ImagePreviewComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour::fromRGB(13, 16, 20));
    g.fillRoundedRectangle(bounds, 6.0f);

    auto content = bounds.reduced(8.0f);
    juce::Rectangle<float> drawArea = content;

    if (image.isValid())
    {
        const auto imageAspect = static_cast<float>(image.getWidth()) / static_cast<float>(image.getHeight());
        const auto contentAspect = content.getWidth() / content.getHeight();

        if (imageAspect > contentAspect)
        {
            const auto height = content.getWidth() / imageAspect;
            drawArea = { content.getX(), content.getCentreY() - height * 0.5f, content.getWidth(), height };
        }
        else
        {
            const auto width = content.getHeight() * imageAspect;
            drawArea = { content.getCentreX() - width * 0.5f, content.getY(), width, content.getHeight() };
        }

        g.drawImage(image, drawArea);
    }
    else
    {
        g.setColour(juce::Colour::fromRGB(42, 47, 56));
        g.fillRoundedRectangle(content, 4.0f);
        g.setColour(juce::Colour::fromRGB(180, 188, 200));
        g.setFont(14.0f);
        g.drawFittedText("Load a photo to drive the scanner oscillator", content.toNearestInt(), juce::Justification::centred, 1);
    }

    if (drawArea.getWidth() <= 1.0f || drawArea.getHeight() <= 1.0f)
    {
        return;
    }

    g.setColour(juce::Colour::fromRGBA(245, 95, 45, 230));

    const auto toPoint = [&drawArea](float u, float v)
    {
        return juce::Point<float>(
            drawArea.getX() + juce::jlimit(0.0f, 1.0f, u) * drawArea.getWidth(),
            drawArea.getY() + juce::jlimit(0.0f, 1.0f, v) * drawArea.getHeight());
    };

    if (scannerMode == 1)
    {
        const auto minX = juce::jmin(ovalX1, ovalX2);
        const auto maxX = juce::jmax(ovalX1, ovalX2);
        const auto minY = juce::jmin(ovalY1, ovalY2);
        const auto maxY = juce::jmax(ovalY1, ovalY2);

        const auto px = drawArea.getX() + minX * drawArea.getWidth();
        const auto py = drawArea.getY() + minY * drawArea.getHeight();
        const auto pw = (maxX - minX) * drawArea.getWidth();
        const auto ph = (maxY - minY) * drawArea.getHeight();

        g.drawEllipse(px, py, pw, ph, 2.2f);
        g.fillEllipse((px + pw * 0.5f) - 4.0f, (py + ph * 0.5f) - 4.0f, 8.0f, 8.0f);
        const auto p1 = normalisedToPoint(ovalX1, ovalY1);
        const auto p2 = normalisedToPoint(ovalX2, ovalY2);
        g.fillEllipse(p1.x - 3.0f, p1.y - 3.0f, 6.0f, 6.0f);
        g.fillEllipse(p2.x - 3.0f, p2.y - 3.0f, 6.0f, 6.0f);
        return;
    }

    if (scannerMode == 2)
    {
        const auto w = juce::jmax(0.001f, rectWidth) * drawArea.getWidth();
        const auto h = juce::jmax(0.001f, rectHeight) * drawArea.getHeight();
        const auto cx = drawArea.getX() + rectX * drawArea.getWidth();
        const auto cy = drawArea.getY() + rectY * drawArea.getHeight();

        g.drawRect(cx - 0.5f * w, cy - 0.5f * h, w, h, 2.2f);
        g.fillEllipse(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
        g.fillEllipse((cx - 0.5f * w) - 3.0f, (cy - 0.5f * h) - 3.0f, 6.0f, 6.0f);
        g.fillEllipse((cx + 0.5f * w) - 3.0f, (cy + 0.5f * h) - 3.0f, 6.0f, 6.0f);
        return;
    }

    if (scannerMode == 3)
    {
        const auto p1 = toPoint(triX1, triY1);
        const auto p2 = toPoint(triX2, triY2);
        const auto p3 = toPoint(triX3, triY3);

        juce::Path triangle;
        triangle.startNewSubPath(p1);
        triangle.lineTo(p2);
        triangle.lineTo(p3);
        triangle.closeSubPath();
        g.strokePath(triangle, juce::PathStrokeType(2.2f));
        g.fillEllipse(p1.x - 3.0f, p1.y - 3.0f, 6.0f, 6.0f);
        g.fillEllipse(p2.x - 3.0f, p2.y - 3.0f, 6.0f, 6.0f);
        g.fillEllipse(p3.x - 3.0f, p3.y - 3.0f, 6.0f, 6.0f);
        return;
    }

    const auto drawLineOverlay = [&](float centerNormX, float centerNormY, float lengthNorm, float angleDegrees)
    {
        const auto centerX = drawArea.getX() + centerNormX * drawArea.getWidth();
        const auto centerY = drawArea.getY() + centerNormY * drawArea.getHeight();
        const auto angleRadians = juce::degreesToRadians(angleDegrees);
        const auto dx = std::cos(angleRadians) * lengthNorm * drawArea.getWidth() * 0.5f;
        const auto dy = std::sin(angleRadians) * lengthNorm * drawArea.getHeight() * 0.5f;

        juce::Line<float> scannerLine(
            juce::Point<float>(centerX - static_cast<float>(dx), centerY - static_cast<float>(dy)),
            juce::Point<float>(centerX + static_cast<float>(dx), centerY + static_cast<float>(dy)));

        g.drawLine(scannerLine, 2.2f);
        g.fillEllipse(centerX - 4.0f, centerY - 4.0f, 8.0f, 8.0f);
        g.fillEllipse(scannerLine.getStartX() - 3.0f, scannerLine.getStartY() - 3.0f, 6.0f, 6.0f);
        g.fillEllipse(scannerLine.getEndX() - 3.0f, scannerLine.getEndY() - 3.0f, 6.0f, 6.0f);
    };

    if (scannerMode == 4)
    {
        drawLineOverlay(propX, propY, propSize, static_cast<float>(juce::radiansToDegrees(propPhase)));
        return;
    }

    drawLineOverlay(lineX, lineY, lineLength, lineAngle);
}

PictureWaveSynthAudioProcessorEditor::PictureWaveSynthAudioProcessorEditor(PictureWaveSynthAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    titleLabel.setText(juce::String(PhotoSynthesisVersion::kDisplayName) + " v" + PhotoSynthesisVersion::kVersion,
                       juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    setupSectionLabel(scannerTitleLabel, "Image Scanner");
    setupSectionLabel(mappingTitleLabel, "RGBA Mapping to Stereo (+/-)");
    setupSectionLabel(envTitleLabel, "Performance");

    addAndMakeVisible(scannerTabs);
    scannerTabs.addTab("Photo Scanner", juce::Colour::fromRGB(44, 50, 62), &photoScannerTabPage, false);
    scannerTabs.addTab("FX", juce::Colour::fromRGB(44, 50, 62), &fxTabPage, false);
    scannerTabs.setCurrentTabIndex(0);

    fxPlaceholderLabel.setText("FX page (coming soon)", juce::dontSendNotification);
    fxPlaceholderLabel.setJustificationType(juce::Justification::centred);
    fxPlaceholderLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(203, 210, 222));
    fxTabPage.addAndMakeVisible(fxPlaceholderLabel);

    loadImageButton.onClick = [this] { openImageChooser(); };
    initButton.onClick = [this]
    {
        audioProcessor.resetToInitialPreset();
        rebuildModeAttachments();
        rebuildActiveLfoAttachments();
        updateModeControlLabelsAndVisibility();
        configureModeSpecificResetBehaviour();
        refreshImagePreview();
        imageStatusLabel.setText("Initial preset loaded", juce::dontSendNotification);
    };
    loadPresetButton.onClick = [this] { loadPresetFromFile(); };
    savePresetButton.onClick = [this] { savePresetToFile(); };
    aboutButton.onClick = []
    {
        const auto message = juce::String("Author: ") + PhotoSynthesisVersion::kAuthor + "\n"
            + "Email: " + PhotoSynthesisVersion::kEmail + "\n"
            + "Version: " + PhotoSynthesisVersion::kVersion;
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            juce::String(PhotoSynthesisVersion::kDisplayName) + " - About",
            message,
            "OK");
    };
    addAndMakeVisible(loadImageButton);
    addAndMakeVisible(initButton);
    addAndMakeVisible(loadPresetButton);
    addAndMakeVisible(savePresetButton);
    addAndMakeVisible(aboutButton);

    imageStatusLabel.setText("No image loaded", juce::dontSendNotification);
    imageStatusLabel.setJustificationType(juce::Justification::centredLeft);
    imageStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(203, 210, 222));
    addAndMakeVisible(imageStatusLabel);
    addAndMakeVisible(imagePreview);
    imagePreview.setParameterChangeCallback([this](const juce::String& paramId, float value)
    {
        updateParameterFromPreview(paramId, value);
    });

    setupScannerSlider(scanXSlider, scanXLabel, "Line X");
    setupScannerSlider(scanYSlider, scanYLabel, "Line Y");
    setupScannerSlider(scanLengthSlider, scanLengthLabel, "Line Length");
    setupScannerSlider(scanAngleSlider, scanAngleLabel, "Line Angle");

    scannerModeLabel.setText("Scanner Mode", juce::dontSendNotification);
    scannerModeLabel.setJustificationType(juce::Justification::centredLeft);
    scannerModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(scannerModeLabel);

    scannerModeCombo.addItem("Line", 1);
    scannerModeCombo.addItem("Oval", 2);
    scannerModeCombo.addItem("Rectangle", 3);
    scannerModeCombo.addItem("Triangle", 4);
    scannerModeCombo.addItem("Propellor", 5);
    scannerModeCombo.onChange = [this]
    {
        rebuildModeAttachments();
        updateModeControlLabelsAndVisibility();
    };
    addAndMakeVisible(scannerModeCombo);

    randomPhaseButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(randomPhaseButton);
    propTempoSyncButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 219, 230));
    propTempoSyncButton.onClick = [this]
    {
        rebuildModeAttachments();
        updateModeControlLabelsAndVisibility();
    };
    addAndMakeVisible(propTempoSyncButton);

    setupModeSpecificSlider(shapeCtrl1Slider, shapeCtrl1Label);
    setupModeSpecificSlider(shapeCtrl2Slider, shapeCtrl2Label);
    setupModeSpecificSlider(shapeCtrl3Slider, shapeCtrl3Label);
    setupModeSpecificSlider(shapeCtrl4Slider, shapeCtrl4Label);
    setupModeSpecificSlider(shapeCtrl5Slider, shapeCtrl5Label);
    setupModeSpecificSlider(shapeCtrl6Slider, shapeCtrl6Label);

    setupLinearSlider(attackSlider, "Attack (ms)");
    setupLinearSlider(decaySlider, "Decay (ms)");
    setupLinearSlider(sustainSlider, "Sustain");
    setupLinearSlider(releaseSlider, "Release (ms)");
    setupLinearSlider(gainSlider, "Gain (dB)");
    gainSlider.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    setupLinearSlider(noteDriftSlider, "Note Drift");
    setupLinearSlider(liveNoteDriftSlider, "Drift Freq");

    attackSlider.setSliderStyle(juce::Slider::LinearVertical);
    attackSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    decaySlider.setSliderStyle(juce::Slider::LinearVertical);
    decaySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    sustainSlider.setSliderStyle(juce::Slider::LinearVertical);
    sustainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    releaseSlider.setSliderStyle(juce::Slider::LinearVertical);
    releaseSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    noteDriftSlider.setSliderStyle(juce::Slider::LinearVertical);
    noteDriftSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    liveNoteDriftSlider.setSliderStyle(juce::Slider::LinearVertical);
    liveNoteDriftSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);

    setupSmallLabel(attackLabel, "Attack");
    setupSmallLabel(decayLabel, "Decay");
    setupSmallLabel(sustainLabel, "Sustain");
    setupSmallLabel(releaseLabel, "Release");
    setupSmallLabel(gainLabel, "Gain");
    setupSmallLabel(noteDriftLabel, "Note Drift");
    setupSmallLabel(liveNoteDriftLabel, "Drift Freq");

    setupMappingSlider(mapRLSlider, mapRLLabel, "R -> L");
    setupMappingSlider(mapGLSlider, mapGLLabel, "G -> L");
    setupMappingSlider(mapBLSlider, mapBLLabel, "B -> L");
    setupMappingSlider(mapALSlider, mapALLabel, "A -> L");
    setupMappingSlider(mapRRSlider, mapRRLabel, "R -> R");
    setupMappingSlider(mapGRSlider, mapGRLabel, "G -> R");
    setupMappingSlider(mapBRSlider, mapBRLabel, "B -> R");
    setupMappingSlider(mapARSlider, mapARLabel, "A -> R");

    mapLeftGroup.setText("Left Speaker");
    mapLeftGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour::fromRGB(65, 73, 88));
    mapLeftGroup.setColour(juce::GroupComponent::textColourId, juce::Colour::fromRGB(231, 235, 242));
    addAndMakeVisible(mapLeftGroup);

    mapRightGroup.setText("Right Speaker");
    mapRightGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour::fromRGB(65, 73, 88));
    mapRightGroup.setColour(juce::GroupComponent::textColourId, juce::Colour::fromRGB(231, 235, 242));
    addAndMakeVisible(mapRightGroup);

    masterGroup.setText("Master");
    masterGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour::fromRGB(65, 73, 88));
    masterGroup.setColour(juce::GroupComponent::textColourId, juce::Colour::fromRGB(231, 235, 242));
    addAndMakeVisible(masterGroup);

    attackAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "attack", attackSlider);
    decayAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "decay", decaySlider);
    sustainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "sustain", sustainSlider);
    releaseAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "release", releaseSlider);
    gainAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "gain", gainSlider);
    noteDriftAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "noteDrift", noteDriftSlider);
    liveNoteDriftAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "liveNoteDrift", liveNoteDriftSlider);

    scanXAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "scanX", scanXSlider);
    scanYAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "scanY", scanYSlider);
    scanLengthAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "scanLength", scanLengthSlider);
    scanAngleAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "scanAngle", scanAngleSlider);
    scannerModeAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "scannerMode", scannerModeCombo);
    randomPhaseAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "randomPhase", randomPhaseButton);
    scanResolutionLabel.setText("Resolution", juce::dontSendNotification);
    scanResolutionLabel.setJustificationType(juce::Justification::centredLeft);
    scanResolutionLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(scanResolutionLabel);
    scanResolutionCombo.addItem("32", 1);
    scanResolutionCombo.addItem("64", 2);
    scanResolutionCombo.addItem("128", 3);
    scanResolutionCombo.addItem("256", 4);
    scanResolutionCombo.addItem("512", 5);
    scanResolutionCombo.addItem("1024", 6);
    scanResolutionCombo.addItem("2048", 7);
    addAndMakeVisible(scanResolutionCombo);
    scanResolutionAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "scanResolution", scanResolutionCombo);
    propTempoSyncAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.parameters, "propTempoSync", propTempoSyncButton);

    mapRLAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapRL", mapRLSlider);
    mapGLAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapGL", mapGLSlider);
    mapBLAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapBL", mapBLSlider);
    mapALAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapAL", mapALSlider);
    mapRRAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapRR", mapRRSlider);
    mapGRAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapGR", mapGRSlider);
    mapBRAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapBR", mapBRSlider);
    mapARAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "mapAR", mapARSlider);

    setupSectionLabel(modulationTitleLabel, "Modulation");

    modulationLfoGroup.setText("LFO Section");
    modulationLfoGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour::fromRGB(65, 73, 88));
    modulationLfoGroup.setColour(juce::GroupComponent::textColourId, juce::Colour::fromRGB(231, 235, 242));
    addAndMakeVisible(modulationLfoGroup);

    modulationRoutingGroup.setText("Routing Section");
    modulationRoutingGroup.setColour(juce::GroupComponent::outlineColourId, juce::Colour::fromRGB(65, 73, 88));
    modulationRoutingGroup.setColour(juce::GroupComponent::textColourId, juce::Colour::fromRGB(231, 235, 242));
    addAndMakeVisible(modulationRoutingGroup);

    modResponseLabel.setText("Response", juce::dontSendNotification);
    modResponseLabel.setJustificationType(juce::Justification::centredLeft);
    modResponseLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    routeSettingsTabPage.addAndMakeVisible(modResponseLabel);
    modResponseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    modResponseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 20);
    routeSettingsTabPage.addAndMakeVisible(modResponseSlider);

    for (int pageIndex = 0; pageIndex < kNumRoutePages; ++pageIndex)
    {
        routeColumnHeaderSource[static_cast<size_t>(pageIndex)].setText("Source", juce::dontSendNotification);
        routeColumnHeaderSource[static_cast<size_t>(pageIndex)].setJustificationType(juce::Justification::centredLeft);
        routeColumnHeaderSource[static_cast<size_t>(pageIndex)].setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
        routeTabPages[static_cast<size_t>(pageIndex)].addAndMakeVisible(routeColumnHeaderSource[static_cast<size_t>(pageIndex)]);

        routeColumnHeaderDestination[static_cast<size_t>(pageIndex)].setText("Destination", juce::dontSendNotification);
        routeColumnHeaderDestination[static_cast<size_t>(pageIndex)].setJustificationType(juce::Justification::centredLeft);
        routeColumnHeaderDestination[static_cast<size_t>(pageIndex)].setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
        routeTabPages[static_cast<size_t>(pageIndex)].addAndMakeVisible(routeColumnHeaderDestination[static_cast<size_t>(pageIndex)]);

        routeColumnHeaderBipolar[static_cast<size_t>(pageIndex)].setText("bip.", juce::dontSendNotification);
        routeColumnHeaderBipolar[static_cast<size_t>(pageIndex)].setJustificationType(juce::Justification::centred);
        routeColumnHeaderBipolar[static_cast<size_t>(pageIndex)].setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
        routeTabPages[static_cast<size_t>(pageIndex)].addAndMakeVisible(routeColumnHeaderBipolar[static_cast<size_t>(pageIndex)]);

        routeColumnHeaderAmount[static_cast<size_t>(pageIndex)].setText("Modulation", juce::dontSendNotification);
        routeColumnHeaderAmount[static_cast<size_t>(pageIndex)].setJustificationType(juce::Justification::centredLeft);
        routeColumnHeaderAmount[static_cast<size_t>(pageIndex)].setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
        routeTabPages[static_cast<size_t>(pageIndex)].addAndMakeVisible(routeColumnHeaderAmount[static_cast<size_t>(pageIndex)]);
    }
    uiZoomLabel.setText("UI Zoom", juce::dontSendNotification);
    uiZoomLabel.setJustificationType(juce::Justification::centredRight);
    uiZoomLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(uiZoomLabel);
    uiZoomCombo.setEditableText(true);
    uiZoomCombo.addItem("25%", 1);
    uiZoomCombo.addItem("50%", 2);
    uiZoomCombo.addItem("75%", 3);
    uiZoomCombo.addItem("85%", 4);
    uiZoomCombo.addItem("100%", 5);
    uiZoomCombo.addItem("115%", 6);
    uiZoomCombo.onChange = [this] { applyUiZoomSelection(); };
    addAndMakeVisible(uiZoomCombo);

    polyphonyLabel.setText("Polyphony", juce::dontSendNotification);
    polyphonyLabel.setJustificationType(juce::Justification::centredRight);
    polyphonyLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(polyphonyLabel);
    polyphonyCombo.addItem("4", 1);
    polyphonyCombo.addItem("8", 2);
    polyphonyCombo.addItem("12", 3);
    polyphonyCombo.addItem("16", 4);
    polyphonyCombo.addItem("24", 5);
    polyphonyCombo.addItem("32", 6);
    addAndMakeVisible(polyphonyCombo);
    polyphonyAttachment = std::make_unique<ComboBoxAttachment>(
        audioProcessor.parameters, "maxVoices", polyphonyCombo);
    modResponseAttachment = std::make_unique<SliderAttachment>(
        audioProcessor.parameters, "modResponseMs", modResponseSlider);

    addAndMakeVisible(lfoTabs);
    for (int i = 0; i < kNumLfos; ++i)
    {
        lfoTabs.addTab(juce::String(i + 1), kLfoTabColours[static_cast<size_t>(i)], &lfoTabPage, false);
    }
    lfoTabs.setTabChangedCallback([this](int newCurrentTabIndex, const juce::String&)
    {
        const auto safeIndex = juce::jlimit(0, kNumLfos - 1, newCurrentTabIndex);
        lfoTabs.setColour(juce::TabbedComponent::backgroundColourId, getLfoContentBackgroundForIndex(safeIndex));
        lfoTabs.repaint();
        lastLfoTabIndex = safeIndex;
        rebuildActiveLfoAttachments();
    });
    lfoTabs.setColour(juce::TabbedComponent::backgroundColourId, getLfoContentBackgroundForIndex(0));

    addAndMakeVisible(routeTabs);
    routeTabs.addTab("Route 1-8", juce::Colour::fromRGB(44, 50, 62), &routeTabPages[0], false);
    routeTabs.addTab("Route 9-16", juce::Colour::fromRGB(44, 50, 62), &routeTabPages[1], false);
    routeTabs.addTab("Route 17-24", juce::Colour::fromRGB(44, 50, 62), &routeTabPages[2], false);
    routeTabs.addTab("Route 25-32", juce::Colour::fromRGB(44, 50, 62), &routeTabPages[3], false);
    routeTabs.addTab("Routing Settings", juce::Colour::fromRGB(44, 50, 62), &routeSettingsTabPage, false);

    setupRotarySlider(lfoRateSlider, "LFO Rate");
    setupRotarySlider(lfoDepthSlider, "LFO Depth");
    setupSmallLabel(lfoRateLabel, "Rate");
    setupSmallLabel(lfoDepthLabel, "Depth");
    lfoTabPage.addAndMakeVisible(lfoRateSlider);
    lfoTabPage.addAndMakeVisible(lfoDepthSlider);
    lfoTabPage.addAndMakeVisible(lfoRateLabel);
    lfoTabPage.addAndMakeVisible(lfoDepthLabel);

    lfoWaveLabel.setText("Wave", juce::dontSendNotification);
    lfoWaveLabel.setJustificationType(juce::Justification::centredLeft);
    lfoWaveLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    lfoTabPage.addAndMakeVisible(lfoWaveLabel);
    lfoWaveCombo.addItem("Sine", 1);
    lfoWaveCombo.addItem("Triangle", 2);
    lfoWaveCombo.addItem("Saw", 3);
    lfoWaveCombo.addItem("Square", 4);
    lfoWaveCombo.addItem("Random Steps", 5);
    lfoWaveCombo.addItem("Random Linear", 6);
    lfoWaveCombo.addItem("Random Perlin", 7);
    lfoWaveCombo.onChange = [this]
    {
        lfoRandomPhasePerVoiceButton.setEnabled(lfoWaveCombo.getSelectedId() <= 4);
    };
    lfoTabPage.addAndMakeVisible(lfoWaveCombo);

    lfoSyncButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 219, 230));
    lfoSyncButton.onClick = [this]
    {
        rebuildActiveLfoAttachments();
        configureModeSpecificResetBehaviour();
    };
    lfoTabPage.addAndMakeVisible(lfoSyncButton);
    lfoRandomPhasePerVoiceButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 219, 230));
    lfoTabPage.addAndMakeVisible(lfoRandomPhasePerVoiceButton);
    lfoTabPage.addAndMakeVisible(lfoVisualizer);

    for (int i = 0; i < kNumModRows; ++i)
    {
        const auto pageIndex = static_cast<size_t>(i / kRoutesPerPage);
        auto& page = routeTabPages[pageIndex];
        const auto routeNumber = i + 1;

        modEnabledButtons[static_cast<size_t>(i)].setButtonText("On");
        modEnabledButtons[static_cast<size_t>(i)].setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(214, 219, 230));
        page.addAndMakeVisible(modEnabledButtons[static_cast<size_t>(i)]);

        if (auto* sourceParam = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.parameters.getParameter("mod1Source")))
        {
            for (int source = 0; source < sourceParam->choices.size(); ++source)
            {
                modSourceCombos[static_cast<size_t>(i)].addItem(sourceParam->choices[source], source + 1);
            }
        }
        else
        {
            for (int source = 1; source <= kNumLfos; ++source)
            {
                modSourceCombos[static_cast<size_t>(i)].addItem("LFO " + juce::String(source), source);
            }
        }
        page.addAndMakeVisible(modSourceCombos[static_cast<size_t>(i)]);

        for (int target = 0; target < static_cast<int>(kModTargetNames.size()); ++target)
        {
            modTargetCombos[static_cast<size_t>(i)].addItem(kModTargetNames[static_cast<size_t>(target)], target + 1);
        }
        page.addAndMakeVisible(modTargetCombos[static_cast<size_t>(i)]);

        modBipolarButtons[static_cast<size_t>(i)].setButtonText({});
        page.addAndMakeVisible(modBipolarButtons[static_cast<size_t>(i)]);

        modAmountSliders[static_cast<size_t>(i)].setSliderStyle(juce::Slider::LinearHorizontal);
        modAmountSliders[static_cast<size_t>(i)].setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
        page.addAndMakeVisible(modAmountSliders[static_cast<size_t>(i)]);

        modRowLabels[static_cast<size_t>(i)].setText("Route " + juce::String(routeNumber), juce::dontSendNotification);
        modRowLabels[static_cast<size_t>(i)].setJustificationType(juce::Justification::centredLeft);
        modRowLabels[static_cast<size_t>(i)].setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
        page.addAndMakeVisible(modRowLabels[static_cast<size_t>(i)]);

        const auto idx = juce::String(i + 1);
        modEnabledAttachments[static_cast<size_t>(i)] = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "mod" + idx + "Enabled", modEnabledButtons[static_cast<size_t>(i)]);
        modSourceAttachments[static_cast<size_t>(i)] = std::make_unique<ComboBoxAttachment>(audioProcessor.parameters, "mod" + idx + "Source", modSourceCombos[static_cast<size_t>(i)]);
        modTargetAttachments[static_cast<size_t>(i)] = std::make_unique<ComboBoxAttachment>(audioProcessor.parameters, "mod" + idx + "Target", modTargetCombos[static_cast<size_t>(i)]);
        modBipolarAttachments[static_cast<size_t>(i)] = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "mod" + idx + "Bipolar", modBipolarButtons[static_cast<size_t>(i)]);
        modAmountAttachments[static_cast<size_t>(i)] = std::make_unique<SliderAttachment>(audioProcessor.parameters, "mod" + idx + "Amount", modAmountSliders[static_cast<size_t>(i)]);
    }

    auto addToPhotoScannerPage = [this](juce::Component& component)
    {
        photoScannerTabPage.addAndMakeVisible(component);
    };

    addToPhotoScannerPage(scannerTitleLabel);
    addToPhotoScannerPage(loadImageButton);
    addToPhotoScannerPage(initButton);
    addToPhotoScannerPage(loadPresetButton);
    addToPhotoScannerPage(savePresetButton);
    addToPhotoScannerPage(imageStatusLabel);
    addToPhotoScannerPage(imagePreview);

    addToPhotoScannerPage(scannerModeLabel);
    addToPhotoScannerPage(scannerModeCombo);
    addToPhotoScannerPage(scanResolutionLabel);
    addToPhotoScannerPage(scanResolutionCombo);
    addToPhotoScannerPage(randomPhaseButton);
    addToPhotoScannerPage(propTempoSyncButton);

    addToPhotoScannerPage(scanXSlider);
    addToPhotoScannerPage(scanYSlider);
    addToPhotoScannerPage(scanLengthSlider);
    addToPhotoScannerPage(scanAngleSlider);
    addToPhotoScannerPage(scanXLabel);
    addToPhotoScannerPage(scanYLabel);
    addToPhotoScannerPage(scanLengthLabel);
    addToPhotoScannerPage(scanAngleLabel);

    addToPhotoScannerPage(shapeCtrl1Slider);
    addToPhotoScannerPage(shapeCtrl2Slider);
    addToPhotoScannerPage(shapeCtrl3Slider);
    addToPhotoScannerPage(shapeCtrl4Slider);
    addToPhotoScannerPage(shapeCtrl5Slider);
    addToPhotoScannerPage(shapeCtrl6Slider);
    addToPhotoScannerPage(shapeCtrl1Label);
    addToPhotoScannerPage(shapeCtrl2Label);
    addToPhotoScannerPage(shapeCtrl3Label);
    addToPhotoScannerPage(shapeCtrl4Label);
    addToPhotoScannerPage(shapeCtrl5Label);
    addToPhotoScannerPage(shapeCtrl6Label);

    updateModeControlLabelsAndVisibility();

    if (scannerModeCombo.getSelectedId() == 0)
    {
        scannerModeCombo.setSelectedId(1, juce::dontSendNotification);
    }
    lfoTabs.setCurrentTabIndex(0);
    routeTabs.setCurrentTabIndex(0);
    rebuildActiveLfoAttachments();
    rebuildModeAttachments();
    updateModeControlLabelsAndVisibility();
    configureResetBehaviour();

    refreshImagePreview();
    startTimerHz(24);
    updateResizeLimitsForDisplay();
    setResizable(false, false);
    const auto [initialWidth, initialHeight] = getIdealEditorSizeForScale(0.75f);
    setSize(initialWidth, initialHeight);
    const auto hasStoredGeometry = restoreEditorGeometryFromState();
    if (uiZoomCombo.getSelectedId() == 0 && uiZoomCombo.getText().isEmpty())
    {
        uiZoomCombo.setSelectedId(3, juce::dontSendNotification);
    }
    applyUiZoomSelection(!hasStoredGeometry);
    storeEditorGeometryToState();
}

PictureWaveSynthAudioProcessorEditor::~PictureWaveSynthAudioProcessorEditor()
{
    storeEditorGeometryToState();
}

void PictureWaveSynthAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    slider.setName(name);
    addAndMakeVisible(slider);
}

void PictureWaveSynthAudioProcessorEditor::setupLinearSlider(juce::Slider& slider, const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    slider.setName(name);
    addAndMakeVisible(slider);
}

void PictureWaveSynthAudioProcessorEditor::setupBipolarSlider(juce::Slider& slider, const juce::String& name)
{
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 20);
    slider.setName(name);
    addAndMakeVisible(slider);
}

void PictureWaveSynthAudioProcessorEditor::setupSmallLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(label);
}

void PictureWaveSynthAudioProcessorEditor::setupSectionLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(239, 242, 247));
    label.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    addAndMakeVisible(label);
}

void PictureWaveSynthAudioProcessorEditor::setupScannerSlider(juce::Slider& slider, juce::Label& label, const juce::String& name)
{
    setupRotarySlider(slider, name);
    setupSmallLabel(label, name);
}

void PictureWaveSynthAudioProcessorEditor::setupMappingSlider(juce::Slider& slider, juce::Label& label, const juce::String& name)
{
    setupBipolarSlider(slider, name);
    if (auto* modulationSlider = dynamic_cast<ModulationSlider*>(&slider))
    {
        modulationSlider->setMappingOverlayEnabled(true);
    }
    if (name.startsWithIgnoreCase("R"))
    {
        slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(230, 74, 74));
        slider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    }
    else if (name.startsWithIgnoreCase("G"))
    {
        slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(90, 210, 120));
        slider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    }
    else if (name.startsWithIgnoreCase("B"))
    {
        slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(84, 151, 255));
        slider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    }
    else if (name.startsWithIgnoreCase("A"))
    {
        slider.setColour(juce::Slider::thumbColourId, juce::Colour::fromRGB(170, 174, 182));
        slider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    }

    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 219, 230));
    addAndMakeVisible(label);
}

void PictureWaveSynthAudioProcessorEditor::setupModeSpecificSlider(juce::Slider& slider, juce::Label& label)
{
    setupScannerSlider(slider, label, "");
}

void PictureWaveSynthAudioProcessorEditor::configureSliderReset(juce::Slider& slider, const char* paramId)
{
    if (auto* param = audioProcessor.parameters.getParameter(paramId))
    {
        slider.setDoubleClickReturnValue(true, param->getDefaultValue());
    }
}

void PictureWaveSynthAudioProcessorEditor::configureComboReset(ResettableComboBox& combo, const char* paramId)
{
    if (auto* param = audioProcessor.parameters.getParameter(paramId))
    {
        const auto defaultNorm = juce::jlimit(0.0f, 1.0f, param->getDefaultValue());
        const auto maxIndex = juce::jmax(0, combo.getNumItems() - 1);
        const auto defaultIndex = static_cast<int>(std::lround(defaultNorm * static_cast<float>(maxIndex))) + 1;
        combo.setResetSelectedId(defaultIndex);
    }
}

void PictureWaveSynthAudioProcessorEditor::configureToggleReset(ResettableToggleButton& button, const char* paramId)
{
    if (auto* param = audioProcessor.parameters.getParameter(paramId))
    {
        button.setResetState(param->getDefaultValue() >= 0.5f);
    }
}

void PictureWaveSynthAudioProcessorEditor::configureResetBehaviour()
{
    configureSliderReset(attackSlider, "attack");
    configureSliderReset(decaySlider, "decay");
    configureSliderReset(sustainSlider, "sustain");
    configureSliderReset(releaseSlider, "release");
    configureSliderReset(gainSlider, "gain");
    configureSliderReset(noteDriftSlider, "noteDrift");
    configureSliderReset(liveNoteDriftSlider, "liveNoteDrift");
    configureSliderReset(scanXSlider, "scanX");
    configureSliderReset(scanYSlider, "scanY");
    configureSliderReset(scanLengthSlider, "scanLength");
    configureSliderReset(scanAngleSlider, "scanAngle");
    configureSliderReset(mapRLSlider, "mapRL");
    configureSliderReset(mapGLSlider, "mapGL");
    configureSliderReset(mapBLSlider, "mapBL");
    configureSliderReset(mapALSlider, "mapAL");
    configureSliderReset(mapRRSlider, "mapRR");
    configureSliderReset(mapGRSlider, "mapGR");
    configureSliderReset(mapBRSlider, "mapBR");
    configureSliderReset(mapARSlider, "mapAR");
    configureSliderReset(modResponseSlider, "modResponseMs");

    configureComboReset(scannerModeCombo, "scannerMode");
    configureComboReset(scanResolutionCombo, "scanResolution");
    configureComboReset(polyphonyCombo, "maxVoices");
    configureComboReset(lfoWaveCombo, "lfo1Wave");
    configureToggleReset(randomPhaseButton, "randomPhase");
    configureToggleReset(propTempoSyncButton, "propTempoSync");
    configureToggleReset(lfoSyncButton, "lfo1Sync");
    configureToggleReset(lfoRandomPhasePerVoiceButton, "lfo1RandomPhasePerVoice");

    for (int i = 0; i < kNumModRows; ++i)
    {
        const auto idx = juce::String(i + 1);
        configureToggleReset(modEnabledButtons[static_cast<size_t>(i)], ("mod" + idx + "Enabled").toRawUTF8());
        configureComboReset(modSourceCombos[static_cast<size_t>(i)], ("mod" + idx + "Source").toRawUTF8());
        configureComboReset(modTargetCombos[static_cast<size_t>(i)], ("mod" + idx + "Target").toRawUTF8());
        configureToggleReset(modBipolarButtons[static_cast<size_t>(i)], ("mod" + idx + "Bipolar").toRawUTF8());
        configureSliderReset(modAmountSliders[static_cast<size_t>(i)], ("mod" + idx + "Amount").toRawUTF8());
    }

    uiZoomCombo.setResetSelectedId(3);
}

void PictureWaveSynthAudioProcessorEditor::configureModeSpecificResetBehaviour()
{
    const auto mode = scannerModeCombo.getSelectedId();
    if (mode == 2)
    {
        configureSliderReset(shapeCtrl1Slider, "ovalX1");
        configureSliderReset(shapeCtrl2Slider, "ovalY1");
        configureSliderReset(shapeCtrl3Slider, "ovalX2");
        configureSliderReset(shapeCtrl4Slider, "ovalY2");
    }
    else if (mode == 3)
    {
        configureSliderReset(shapeCtrl1Slider, "rectX");
        configureSliderReset(shapeCtrl2Slider, "rectY");
        configureSliderReset(shapeCtrl3Slider, "rectWidth");
        configureSliderReset(shapeCtrl4Slider, "rectHeight");
    }
    else if (mode == 4)
    {
        configureSliderReset(shapeCtrl1Slider, "triX1");
        configureSliderReset(shapeCtrl2Slider, "triY1");
        configureSliderReset(shapeCtrl3Slider, "triX2");
        configureSliderReset(shapeCtrl4Slider, "triY2");
        configureSliderReset(shapeCtrl5Slider, "triX3");
        configureSliderReset(shapeCtrl6Slider, "triY3");
    }
    else if (mode == 5)
    {
        configureSliderReset(shapeCtrl1Slider, "propX");
        configureSliderReset(shapeCtrl2Slider, "propY");
        configureSliderReset(shapeCtrl3Slider, "propSize");
        configureSliderReset(shapeCtrl4Slider, propTempoSyncButton.getToggleState() ? "propSyncDivision" : "propSpeed");
    }
}

void PictureWaveSynthAudioProcessorEditor::openImageChooser()
{
    imageChooser = std::make_unique<juce::FileChooser>(
        "Load image for scanner wavetable",
        juce::File{},
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    imageChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (!selected.existsAsFile())
        {
            return;
        }

        juce::String error;
        if (audioProcessor.loadImageFromFile(selected, error))
        {
            imageStatusLabel.setText("Loaded: " + selected.getFileName(), juce::dontSendNotification);
            refreshImagePreview();
        }
        else
        {
            imageStatusLabel.setText("Load failed: " + error, juce::dontSendNotification);
        }
    });
}

void PictureWaveSynthAudioProcessorEditor::savePresetToFile()
{
    presetChooser = std::make_unique<juce::FileChooser>(
        "Save PhotoSynthesis preset",
        juce::File{},
        "*.pspreset");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    presetChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto target = chooser.getResult();
        if (target == juce::File{})
        {
            return;
        }

        if (!target.hasFileExtension("pspreset"))
        {
            target = target.withFileExtension(".pspreset");
        }

        juce::MemoryBlock stateData;
        audioProcessor.getStateInformation(stateData);
        if (target.replaceWithData(stateData.getData(), stateData.getSize()))
        {
            imageStatusLabel.setText("Preset saved: " + target.getFileName(), juce::dontSendNotification);
        }
        else
        {
            imageStatusLabel.setText("Preset save failed", juce::dontSendNotification);
        }
    });
}

void PictureWaveSynthAudioProcessorEditor::loadPresetFromFile()
{
    presetChooser = std::make_unique<juce::FileChooser>(
        "Load PhotoSynthesis preset",
        juce::File{},
        "*.pspreset");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    presetChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        const auto selected = chooser.getResult();
        if (!selected.existsAsFile())
        {
            return;
        }

        juce::MemoryBlock stateData;
        if (!selected.loadFileAsData(stateData) || stateData.getSize() == 0)
        {
            imageStatusLabel.setText("Preset load failed", juce::dontSendNotification);
            return;
        }

        audioProcessor.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
        const auto hasStoredGeometry = restoreEditorGeometryFromState();
        applyUiZoomSelection(!hasStoredGeometry);
        rebuildModeAttachments();
        rebuildActiveLfoAttachments();
        updateModeControlLabelsAndVisibility();
        refreshImagePreview();
        imageStatusLabel.setText("Preset loaded: " + selected.getFileName(), juce::dontSendNotification);
    });
}

void PictureWaveSynthAudioProcessorEditor::updateParameterFromPreview(const juce::String& paramId, float value)
{
    if (auto* param = audioProcessor.parameters.getParameter(paramId))
    {
        const auto norm = param->convertTo0to1(value);
        param->beginChangeGesture();
        param->setValueNotifyingHost(norm);
        param->endChangeGesture();
    }
}

void PictureWaveSynthAudioProcessorEditor::refreshImagePreview()
{
    const auto imageCopy = audioProcessor.getLoadedImageCopy();
    const auto hasImage = imageCopy.isValid();

    if (hasImage)
    {
        const auto status = imageStatusLabel.getText();
        if (status == "No image loaded")
        {
            imageStatusLabel.setText("Image embedded in project state", juce::dontSendNotification);
        }
    }
    else if (!imageStatusLabel.getText().startsWith("Load failed:"))
    {
        imageStatusLabel.setText("No image loaded", juce::dontSendNotification);
    }

    imagePreview.setImage(imageCopy);

    const auto propPhase = audioProcessor.getPropellorPhase();
    const auto mode = juce::jlimit(0, 4, static_cast<int>(std::lround(audioProcessor.parameters.getRawParameterValue("scannerMode")->load())));

    imagePreview.setScanner(
        mode,
        audioProcessor.getEffectiveParameterValue("scanX"),
        audioProcessor.getEffectiveParameterValue("scanY"),
        audioProcessor.getEffectiveParameterValue("scanLength"),
        audioProcessor.getEffectiveParameterValue("scanAngle"),
        audioProcessor.getEffectiveParameterValue("ovalX1"),
        audioProcessor.getEffectiveParameterValue("ovalY1"),
        audioProcessor.getEffectiveParameterValue("ovalX2"),
        audioProcessor.getEffectiveParameterValue("ovalY2"),
        audioProcessor.getEffectiveParameterValue("rectX"),
        audioProcessor.getEffectiveParameterValue("rectY"),
        audioProcessor.getEffectiveParameterValue("rectWidth"),
        audioProcessor.getEffectiveParameterValue("rectHeight"),
        audioProcessor.getEffectiveParameterValue("triX1"),
        audioProcessor.getEffectiveParameterValue("triY1"),
        audioProcessor.getEffectiveParameterValue("triX2"),
        audioProcessor.getEffectiveParameterValue("triY2"),
        audioProcessor.getEffectiveParameterValue("triX3"),
        audioProcessor.getEffectiveParameterValue("triY3"),
        audioProcessor.getEffectiveParameterValue("propX"),
        audioProcessor.getEffectiveParameterValue("propY"),
        audioProcessor.getEffectiveParameterValue("propSize"),
        propPhase);
}

void PictureWaveSynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(18, 21, 27));

    const auto safeScale = juce::jmax(0.01f, uiScaleFactor);
    const auto layoutWidth = static_cast<int>(std::round(static_cast<float>(getWidth()) / safeScale));
    const auto layoutHeight = static_cast<int>(std::round(static_cast<float>(getHeight()) / safeScale));
    g.addTransform(juce::AffineTransform::scale(safeScale));

    const int topBlockTop = 48;
    const int topBlockHeight = 454;
    const int midBlockTop = 516;
    const int blockGap = 12;
    const int bottomMargin = 12;
    const int modulationHeight = juce::jlimit(220, 340, layoutHeight / 3);
    const int modulationTop = layoutHeight - bottomMargin - modulationHeight;
    const int midBlockHeight = juce::jmax(220, modulationTop - midBlockTop - blockGap);

    g.setColour(juce::Colour::fromRGB(30, 35, 44));
    g.fillRoundedRectangle(12.0f, static_cast<float>(topBlockTop), static_cast<float>(layoutWidth - 24), static_cast<float>(topBlockHeight), 8.0f);

    constexpr int kMidPanelLeftX = 12;
    constexpr int kMidPanelGap = 14;
    constexpr int kMapPanelWidth = 698;
    const int perfPanelX = kMidPanelLeftX + kMapPanelWidth + kMidPanelGap;

    g.setColour(juce::Colour::fromRGB(29, 33, 42));
    g.fillRoundedRectangle(static_cast<float>(kMidPanelLeftX), static_cast<float>(midBlockTop), static_cast<float>(kMapPanelWidth), static_cast<float>(midBlockHeight), 8.0f);

    g.setColour(juce::Colour::fromRGB(28, 32, 40));
    g.fillRoundedRectangle(static_cast<float>(perfPanelX), static_cast<float>(midBlockTop), static_cast<float>(layoutWidth - perfPanelX - 12), static_cast<float>(midBlockHeight), 8.0f);

    g.setColour(juce::Colour::fromRGB(27, 31, 39));
    g.fillRoundedRectangle(12.0f, static_cast<float>(modulationTop), static_cast<float>(layoutWidth - 24), static_cast<float>(modulationHeight), 8.0f);
}

void PictureWaveSynthAudioProcessorEditor::resized()
{
    const auto safeScale = juce::jmax(0.01f, uiScaleFactor);
    const int layoutWidth = static_cast<int>(std::round(static_cast<float>(getWidth()) / safeScale));
    const int layoutHeight = static_cast<int>(std::round(static_cast<float>(getHeight()) / safeScale));

    const int midBlockTop = 516;
    const int blockGap = 12;
    const int bottomMargin = 12;
    const int modulationHeight = juce::jlimit(220, 340, layoutHeight / 3);
    const int modulationTop = layoutHeight - bottomMargin - modulationHeight;
    const int midBlockHeight = juce::jmax(220, modulationTop - midBlockTop - blockGap);

    titleLabel.setBounds(0, 10, layoutWidth, 30);

    const int topPanelY = 84;
    const int topPanelH = 402;
    const int masterGroupX = 24;
    const int masterGroupW = 108;
    masterGroup.setBounds(masterGroupX, topPanelY, masterGroupW, topPanelH);
    gainSlider.setBounds(masterGroupX + 20, topPanelY + 38, masterGroupW - 40, topPanelH - 86);
    gainLabel.setBounds(masterGroupX + 10, topPanelY + topPanelH - 36, masterGroupW - 20, 18);

    const int scannerTabsX = masterGroupX + masterGroupW + 10;
    const int scannerTabsW = layoutWidth - scannerTabsX - 24;
    scannerTabs.setBounds(scannerTabsX, topPanelY, scannerTabsW, topPanelH);

    const int pagePad = 10;
    const int buttonY = 6;
    scannerTitleLabel.setBounds(pagePad, buttonY + 2, 160, 22);
    loadImageButton.setBounds(pagePad + 166, buttonY, 112, 26);
    initButton.setBounds(pagePad + 286, buttonY, 80, 26);
    loadPresetButton.setBounds(pagePad + 374, buttonY, 112, 26);
    savePresetButton.setBounds(pagePad + 494, buttonY, 112, 26);

    const auto photoW = photoScannerTabPage.getWidth();
    const auto photoH = photoScannerTabPage.getHeight();
    imageStatusLabel.setBounds(pagePad, 34, juce::jmax(200, photoW - 2 * pagePad), 22);

    const int imageTop = 60;
    const int imageX = pagePad;
    const int imageH = juce::jmax(200, photoH - imageTop - pagePad);
    const int imageW = juce::jlimit(380, 650, photoW - 360);
    imagePreview.setBounds(imageX, imageTop, imageW, imageH);

    const int scannerStartX = imageX + imageW + 12;
    const int scannerLabelW = 114;
    const int scannerComboW = juce::jmax(110, photoW - scannerStartX - scannerLabelW - pagePad);
    scannerModeLabel.setBounds(scannerStartX, 60, scannerLabelW, 22);
    scannerModeCombo.setBounds(scannerStartX + scannerLabelW, 60, scannerComboW, 26);
    scanResolutionLabel.setBounds(scannerStartX, 88, scannerLabelW, 22);
    scanResolutionCombo.setBounds(scannerStartX + scannerLabelW, 88, juce::jmin(140, scannerComboW), 26);
    randomPhaseButton.setBounds(scannerStartX, 120, juce::jmin(180, scannerComboW + scannerLabelW), 22);
    propTempoSyncButton.setBounds(scannerStartX + 190, 120, juce::jmin(130, juce::jmax(100, photoW - scannerStartX - 190 - pagePad)), 22);

    const int scannerKnobTop = 146;
    const int scannerKnobGap = 8;
    const int scannerKnobAreaW = juce::jmax(220, photoW - scannerStartX - pagePad);
    const int scannerKnobSize = juce::jlimit(58, 76, (scannerKnobAreaW - scannerKnobGap * 3) / 4);
    const int scannerLabelOffset = scannerKnobSize + 2;
    const int scannerRowStep = scannerKnobSize + 22;
    const int shapeBaseTop = scannerKnobTop;

    scanXSlider.setBounds(scannerStartX + 0 * (scannerKnobSize + scannerKnobGap), scannerKnobTop, scannerKnobSize, scannerKnobSize);
    scanYSlider.setBounds(scannerStartX + 1 * (scannerKnobSize + scannerKnobGap), scannerKnobTop, scannerKnobSize, scannerKnobSize);
    scanLengthSlider.setBounds(scannerStartX + 2 * (scannerKnobSize + scannerKnobGap), scannerKnobTop, scannerKnobSize, scannerKnobSize);
    scanAngleSlider.setBounds(scannerStartX + 3 * (scannerKnobSize + scannerKnobGap), scannerKnobTop, scannerKnobSize, scannerKnobSize);

    scanXLabel.setBounds(scannerStartX + 0 * (scannerKnobSize + scannerKnobGap), scannerKnobTop + scannerLabelOffset, scannerKnobSize, 18);
    scanYLabel.setBounds(scannerStartX + 1 * (scannerKnobSize + scannerKnobGap), scannerKnobTop + scannerLabelOffset, scannerKnobSize, 18);
    scanLengthLabel.setBounds(scannerStartX + 2 * (scannerKnobSize + scannerKnobGap), scannerKnobTop + scannerLabelOffset, scannerKnobSize, 18);
    scanAngleLabel.setBounds(scannerStartX + 3 * (scannerKnobSize + scannerKnobGap), scannerKnobTop + scannerLabelOffset, scannerKnobSize, 18);

    shapeCtrl1Slider.setBounds(scannerStartX + 0 * (scannerKnobSize + scannerKnobGap), shapeBaseTop, scannerKnobSize, scannerKnobSize);
    shapeCtrl2Slider.setBounds(scannerStartX + 1 * (scannerKnobSize + scannerKnobGap), shapeBaseTop, scannerKnobSize, scannerKnobSize);
    shapeCtrl3Slider.setBounds(scannerStartX + 2 * (scannerKnobSize + scannerKnobGap), shapeBaseTop, scannerKnobSize, scannerKnobSize);
    shapeCtrl4Slider.setBounds(scannerStartX + 0 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerRowStep, scannerKnobSize, scannerKnobSize);
    shapeCtrl5Slider.setBounds(scannerStartX + 1 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerRowStep, scannerKnobSize, scannerKnobSize);
    shapeCtrl6Slider.setBounds(scannerStartX + 2 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerRowStep, scannerKnobSize, scannerKnobSize);

    shapeCtrl1Label.setBounds(scannerStartX + 0 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerLabelOffset, scannerKnobSize, 18);
    shapeCtrl2Label.setBounds(scannerStartX + 1 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerLabelOffset, scannerKnobSize, 18);
    shapeCtrl3Label.setBounds(scannerStartX + 2 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerLabelOffset, scannerKnobSize, 18);
    shapeCtrl4Label.setBounds(scannerStartX + 0 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerRowStep + scannerLabelOffset, scannerKnobSize, 18);
    shapeCtrl5Label.setBounds(scannerStartX + 1 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerRowStep + scannerLabelOffset, scannerKnobSize, 18);
    shapeCtrl6Label.setBounds(scannerStartX + 2 * (scannerKnobSize + scannerKnobGap), shapeBaseTop + scannerRowStep + scannerLabelOffset, scannerKnobSize, 18);

    fxPlaceholderLabel.setBounds(20, 18, juce::jmax(220, fxTabPage.getWidth() - 40), juce::jmax(100, fxTabPage.getHeight() - 36));

    constexpr int kMidPanelLeftX = 12;
    constexpr int kMapPanelWidth = 698;
    constexpr int kMidPanelGap = 14;
    const int perfX = kMidPanelLeftX + kMapPanelWidth + kMidPanelGap;
    const int perfW = layoutWidth - perfX - 12;

    mappingTitleLabel.setBounds(24, midBlockTop + 12, 340, 22);
    envTitleLabel.setBounds(perfX + 12, midBlockTop + 12, juce::jmax(160, perfW - 24), 22);

    const int mapTop = midBlockTop + 44;
    const int mapGroupY = midBlockTop + 34;
    const int mapGroupH = juce::jmax(150, midBlockHeight - 44);
    const int mapGroupW = 332;
    const int mapInnerX = 16;
    const int mapSliderW = 62;
    const int mapSliderGap = 8;
    const int mapSliderH = juce::jmax(108, mapGroupH - 94);
    const int mapLabelTop = mapTop + mapSliderH + 8;

    mapLeftGroup.setBounds(20, mapGroupY, mapGroupW, mapGroupH);
    mapRightGroup.setBounds(360, mapGroupY, mapGroupW, mapGroupH);

    auto placeMapGroupSlider = [mapLabelTop, mapSliderH](int x, juce::Slider& slider, juce::Label& label)
    {
        slider.setBounds(x, mapTop, mapSliderW, mapSliderH);
        label.setBounds(x - 4, mapLabelTop, mapSliderW + 8, 18);
    };

    placeMapGroupSlider(20 + mapInnerX + 0 * (mapSliderW + mapSliderGap), mapRLSlider, mapRLLabel);
    placeMapGroupSlider(20 + mapInnerX + 1 * (mapSliderW + mapSliderGap), mapGLSlider, mapGLLabel);
    placeMapGroupSlider(20 + mapInnerX + 2 * (mapSliderW + mapSliderGap), mapBLSlider, mapBLLabel);
    placeMapGroupSlider(20 + mapInnerX + 3 * (mapSliderW + mapSliderGap), mapALSlider, mapALLabel);
    placeMapGroupSlider(360 + mapInnerX + 0 * (mapSliderW + mapSliderGap), mapRRSlider, mapRRLabel);
    placeMapGroupSlider(360 + mapInnerX + 1 * (mapSliderW + mapSliderGap), mapGRSlider, mapGRLabel);
    placeMapGroupSlider(360 + mapInnerX + 2 * (mapSliderW + mapSliderGap), mapBRSlider, mapBRLabel);
    placeMapGroupSlider(360 + mapInnerX + 3 * (mapSliderW + mapSliderGap), mapARSlider, mapARLabel);

    const int perfTop = midBlockTop + 40;
    const int perfLabelH = 18;
    const int perfInnerX = perfX + 12;
    const int perfInnerW = juce::jmax(280, perfW - 24);
    const int controlCount = 6;
    int knobW = 60;
    int knobGap = (perfInnerW - controlCount * knobW) / (controlCount - 1);
    if (knobGap < 4)
    {
        knobW = juce::jmax(48, (perfInnerW - 4 * (controlCount - 1)) / controlCount);
        knobGap = juce::jmax(4, (perfInnerW - controlCount * knobW) / (controlCount - 1));
    }
    else
    {
        knobGap = juce::jmin(12, knobGap);
    }

    const int knobTop = perfTop + 18;
    const int knobH = juce::jmax(70, midBlockHeight - 92);
    const int labelTop = knobTop + knobH + 4;

    auto layoutPerfControl = [knobW, knobH, knobGap, labelTop](int index, juce::Slider& slider, juce::Label& label)
    {
        const int x = perfInnerX + index * (knobW + knobGap);
        slider.setBounds(x, knobTop, knobW, knobH);
        label.setBounds(x - 6, labelTop, knobW + 12, perfLabelH);
    };

    layoutPerfControl(0, attackSlider, attackLabel);
    layoutPerfControl(1, decaySlider, decayLabel);
    layoutPerfControl(2, sustainSlider, sustainLabel);
    layoutPerfControl(3, releaseSlider, releaseLabel);
    layoutPerfControl(4, noteDriftSlider, noteDriftLabel);
    layoutPerfControl(5, liveNoteDriftSlider, liveNoteDriftLabel);

    modulationTitleLabel.setBounds(24, modulationTop + 12, 260, 22);

    const int modulationInnerTop = modulationTop + 34;
    const int modulationInnerHeight = juce::jmax(160, modulationHeight - 40);
    const int lfoGroupX = 20;
    const int lfoGroupY = modulationInnerTop;
    const int lfoGroupW = 490;
    const int lfoGroupH = modulationInnerHeight;
    const int routingGroupX = 520;
    const int routingGroupW = juce::jmax(420, layoutWidth - routingGroupX - 20);
    const int routingGroupH = modulationInnerHeight;

    modulationLfoGroup.setBounds(lfoGroupX, lfoGroupY, lfoGroupW, lfoGroupH);
    modulationRoutingGroup.setBounds(routingGroupX, lfoGroupY, routingGroupW, routingGroupH);

    lfoTabs.setBounds(lfoGroupX + 14, lfoGroupY + 28, lfoGroupW - 28, lfoGroupH - 42);

    constexpr int lfoContentYOffset = -15;
    const int lfoControlTop = 28 + lfoContentYOffset;
    const int lfoControlW = 96;
    const int lfoControlH = juce::jmin(86, juce::jmax(66, lfoTabs.getHeight() - 154));
    const int lfoControlGap = 10;
    lfoRateSlider.setBounds(14, lfoControlTop, lfoControlW, lfoControlH);
    lfoDepthSlider.setBounds(14 + (lfoControlW + lfoControlGap), lfoControlTop, lfoControlW, lfoControlH);
    lfoRateLabel.setBounds(14, lfoControlTop + 82, lfoControlW, 18);
    lfoDepthLabel.setBounds(14 + (lfoControlW + lfoControlGap), lfoControlTop + 82, lfoControlW, 18);

    lfoWaveLabel.setBounds(14 + 2 * (lfoControlW + lfoControlGap), lfoControlTop + 18, 70, 20);
    lfoWaveCombo.setBounds(14 + 2 * (lfoControlW + lfoControlGap) + 72, lfoControlTop + 16, 120, 24);
    lfoSyncButton.setBounds(14 + 2 * (lfoControlW + lfoControlGap), lfoControlTop + 52, 200, 22);
    lfoRandomPhasePerVoiceButton.setBounds(14 + 2 * (lfoControlW + lfoControlGap), lfoControlTop + 80, 220, 22);

    const int lfoVisualizerBaseY = lfoControlTop + lfoControlH + 46;
    const int lfoVisualizerY = lfoVisualizerBaseY - 10;
    const int lfoVisualizerH = juce::jmax(42, lfoTabs.getHeight() - lfoVisualizerBaseY - 25);
    lfoVisualizer.setBounds(14, lfoVisualizerY, lfoTabs.getWidth() - 28, lfoVisualizerH);

    routeTabs.setBounds(routingGroupX + 14, lfoGroupY + 40, routingGroupW - 28, routingGroupH - 50);

    modResponseLabel.setBounds(14, 16, 84, 22);
    modResponseSlider.setBounds(102, 16, juce::jmax(220, routeSettingsTabPage.getWidth() - 178), 22);

    const auto layoutRoutePage = [&](int pageIndex)
    {
        auto& page = routeTabPages[static_cast<size_t>(pageIndex)];
        const int pageW = page.getWidth();
        const int pageH = page.getHeight();
        const int firstRowY = 30;
        const int headerY = 8;
        const int rowHeight = juce::jmax(22, (pageH - firstRowY - 10) / kRoutesPerPage);
        const int rowLabelX = 10;
        const int rowLabelW = 66;
        const int onX = 78;
        const int onW = 46;
        const int sourceX = 130;
        const int sourceW = 90;
        const int targetX = 236;
        const int targetW = juce::jmax(140, juce::jmin(210, pageW - 470));
        const int bipX = targetX + targetW + 8;
        const int bipW = 34;
        const int amountX = bipX + bipW + 8;
        const int amountW = juce::jmax(150, pageW - amountX - 12);

        routeColumnHeaderSource[static_cast<size_t>(pageIndex)].setBounds(sourceX, headerY, sourceW, 18);
        routeColumnHeaderDestination[static_cast<size_t>(pageIndex)].setBounds(targetX, headerY, targetW, 18);
        routeColumnHeaderBipolar[static_cast<size_t>(pageIndex)].setBounds(bipX, headerY, bipW, 18);
        routeColumnHeaderAmount[static_cast<size_t>(pageIndex)].setBounds(amountX, headerY, amountW, 18);

        for (int row = 0; row < kRoutesPerPage; ++row)
        {
            const int routeIndex = pageIndex * kRoutesPerPage + row;
            const int y = firstRowY + row * rowHeight;
            modRowLabels[static_cast<size_t>(routeIndex)].setBounds(rowLabelX, y + 6, rowLabelW, 20);
            modEnabledButtons[static_cast<size_t>(routeIndex)].setBounds(onX, y + 4, onW, 22);
            modSourceCombos[static_cast<size_t>(routeIndex)].setBounds(sourceX, y + 3, sourceW, 24);
            modTargetCombos[static_cast<size_t>(routeIndex)].setBounds(targetX, y + 3, targetW, 24);
            modBipolarButtons[static_cast<size_t>(routeIndex)].setBounds(bipX + 8, y + 6, 18, 18);
            modAmountSliders[static_cast<size_t>(routeIndex)].setBounds(amountX, y + 3, amountW, 24);
        }
    };

    for (int pageIndex = 0; pageIndex < kNumRoutePages; ++pageIndex)
    {
        layoutRoutePage(pageIndex);
    }

    polyphonyLabel.setBounds(24, 56, 90, 22);
    polyphonyCombo.setBounds(118, 56, 66, 24);
    uiZoomLabel.setBounds(198, 56, 74, 22);
    uiZoomCombo.setBounds(278, 56, 96, 24);
    aboutButton.setBounds(layoutWidth - 100, 56, 76, 24);

    storeEditorGeometryToState();
}

void PictureWaveSynthAudioProcessorEditor::timerCallback()
{
    const auto currentTab = lfoTabs.getCurrentTabIndex();

    const auto lfoRateValue = static_cast<float>(lfoRateSlider.getValue());
    const auto lfoDepth = static_cast<float>(lfoDepthSlider.getValue());
    const auto waveId = juce::jmax(1, lfoWaveCombo.getSelectedId());
    auto rateHz = juce::jlimit(0.05f, 20.0f, lfoRateValue);
    if (lfoSyncButton.getToggleState())
    {
        const auto divisionIndex = static_cast<int>(std::lround(lfoRateValue));
        rateHz = static_cast<float>((120.0 / 60.0) / beatsPerCycleFromSyncDivision(divisionIndex));
    }
    lfoVisualizerPhase = std::fmod(lfoVisualizerPhase + rateHz / 24.0f, 1.0f);
    lfoVisualizer.setVisualState(
        lfoVisualizerPhase,
        juce::jlimit(0.0f, 1.0f, lfoDepth),
        waveId,
        getLfoTabColourForIndex(currentTab));

    refreshModulationVisuals();
    refreshImagePreview();
}

void PictureWaveSynthAudioProcessorEditor::refreshModulationVisuals()
{
    const auto updateSlider = [this](ModulationSlider& slider, const char* paramId)
    {
        slider.setModulationVisual(audioProcessor.getModulationAmountForParameter(paramId));
        if (auto* param = audioProcessor.parameters.getParameter(paramId))
        {
            slider.setEffectiveNormalisedValue(param->convertTo0to1(audioProcessor.getEffectiveParameterValue(paramId)));
        }
    };

    updateSlider(attackSlider, "attack");
    updateSlider(decaySlider, "decay");
    updateSlider(sustainSlider, "sustain");
    updateSlider(releaseSlider, "release");
    updateSlider(gainSlider, "gain");
    updateSlider(noteDriftSlider, "noteDrift");
    updateSlider(liveNoteDriftSlider, "liveNoteDrift");

    updateSlider(scanXSlider, "scanX");
    updateSlider(scanYSlider, "scanY");
    updateSlider(scanLengthSlider, "scanLength");
    updateSlider(scanAngleSlider, "scanAngle");

    updateSlider(mapRLSlider, "mapRL");
    updateSlider(mapGLSlider, "mapGL");
    updateSlider(mapBLSlider, "mapBL");
    updateSlider(mapALSlider, "mapAL");
    updateSlider(mapRRSlider, "mapRR");
    updateSlider(mapGRSlider, "mapGR");
    updateSlider(mapBRSlider, "mapBR");
    updateSlider(mapARSlider, "mapAR");

    shapeCtrl1Slider.setModulationVisual(0.0f);
    shapeCtrl2Slider.setModulationVisual(0.0f);
    shapeCtrl3Slider.setModulationVisual(0.0f);
    shapeCtrl4Slider.setModulationVisual(0.0f);
    shapeCtrl5Slider.setModulationVisual(0.0f);
    shapeCtrl6Slider.setModulationVisual(0.0f);
    shapeCtrl1Slider.setEffectiveNormalisedValue(0.5f);
    shapeCtrl2Slider.setEffectiveNormalisedValue(0.5f);
    shapeCtrl3Slider.setEffectiveNormalisedValue(0.5f);
    shapeCtrl4Slider.setEffectiveNormalisedValue(0.5f);
    shapeCtrl5Slider.setEffectiveNormalisedValue(0.5f);
    shapeCtrl6Slider.setEffectiveNormalisedValue(0.5f);

    const auto mode = scannerModeCombo.getSelectedId();
    if (mode == 2)
    {
        updateSlider(shapeCtrl1Slider, "ovalX1");
        updateSlider(shapeCtrl2Slider, "ovalY1");
        updateSlider(shapeCtrl3Slider, "ovalX2");
        updateSlider(shapeCtrl4Slider, "ovalY2");
    }
    else if (mode == 3)
    {
        updateSlider(shapeCtrl1Slider, "rectX");
        updateSlider(shapeCtrl2Slider, "rectY");
        updateSlider(shapeCtrl3Slider, "rectWidth");
        updateSlider(shapeCtrl4Slider, "rectHeight");
    }
    else if (mode == 4)
    {
        updateSlider(shapeCtrl1Slider, "triX1");
        updateSlider(shapeCtrl2Slider, "triY1");
        updateSlider(shapeCtrl3Slider, "triX2");
        updateSlider(shapeCtrl4Slider, "triY2");
        updateSlider(shapeCtrl5Slider, "triX3");
        updateSlider(shapeCtrl6Slider, "triY3");
    }
    else if (mode == 5)
    {
        updateSlider(shapeCtrl1Slider, "propX");
        updateSlider(shapeCtrl2Slider, "propY");
        updateSlider(shapeCtrl3Slider, "propSize");
        if (!propTempoSyncButton.getToggleState())
        {
            updateSlider(shapeCtrl4Slider, "propSpeed");
        }
    }
}

void PictureWaveSynthAudioProcessorEditor::rebuildActiveLfoAttachments()
{
    activeLfoRateAttachment.reset();
    activeLfoDepthAttachment.reset();
    activeLfoWaveAttachment.reset();
    activeLfoSyncAttachment.reset();
    activeLfoRandomPhasePerVoiceAttachment.reset();

    lfoRateSlider.textFromValueFunction = nullptr;
    lfoRateSlider.valueFromTextFunction = nullptr;

    const auto lfoIndex = juce::jlimit(0, kNumLfos - 1, lfoTabs.getCurrentTabIndex()) + 1;
    const auto idx = juce::String(lfoIndex);

    auto* syncParam = audioProcessor.parameters.getRawParameterValue("lfo" + idx + "Sync");
    const auto syncEnabled = syncParam != nullptr && syncParam->load() > 0.5f;

    if (syncEnabled)
    {
        lfoRateLabel.setText("Division", juce::dontSendNotification);
        lfoRateSlider.textFromValueFunction = [](double value)
        {
            return syncDivisionTextFromIndex(static_cast<int>(std::lround(value)));
        };
        lfoRateSlider.valueFromTextFunction = [](const juce::String& text)
        {
            for (int i = 0; i < static_cast<int>(kSyncDivisionNames.size()); ++i)
            {
                if (text.trim().equalsIgnoreCase(kSyncDivisionNames[static_cast<size_t>(i)]))
                {
                    return static_cast<double>(i);
                }
            }

            return text.getDoubleValue();
        };
        activeLfoRateAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "lfo" + idx + "Division", lfoRateSlider);
    }
    else
    {
        lfoRateLabel.setText("Rate", juce::dontSendNotification);
        activeLfoRateAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "lfo" + idx + "Rate", lfoRateSlider);
    }

    activeLfoDepthAttachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "lfo" + idx + "Depth", lfoDepthSlider);
    activeLfoWaveAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.parameters, "lfo" + idx + "Wave", lfoWaveCombo);
    activeLfoSyncAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "lfo" + idx + "Sync", lfoSyncButton);
    activeLfoRandomPhasePerVoiceAttachment = std::make_unique<ButtonAttachment>(audioProcessor.parameters, "lfo" + idx + "RandomPhasePerVoice", lfoRandomPhasePerVoiceButton);
    lfoRandomPhasePerVoiceButton.setEnabled(lfoWaveCombo.getSelectedId() <= 4);
}

void PictureWaveSynthAudioProcessorEditor::updateResizeLimitsForDisplay()
{
    auto& displays = juce::Desktop::getInstance().getDisplays();
    juce::Rectangle<int> userArea;
    if (auto* atMouse = displays.getDisplayForPoint(juce::Desktop::getMousePosition()))
    {
        userArea = atMouse->userArea;
    }

    if (userArea.isEmpty())
    {
        if (auto* primary = displays.getPrimaryDisplay())
        {
            userArea = primary->userArea;
        }
    }

    if (userArea.isEmpty())
    {
        userArea = { 0, 0, kBaseEditorWidth, kBaseEditorHeight };
    }

    maxEditorWidth = juce::jmax(520, userArea.getWidth() - 40);
    maxEditorHeight = juce::jmax(420, userArea.getHeight() - 40);

    minEditorWidth = juce::jmin(maxEditorWidth, juce::jmax(260, static_cast<int>(std::round(kFitBaseEditorWidth * 0.25f))));
    minEditorHeight = juce::jmin(maxEditorHeight, juce::jmax(220, static_cast<int>(std::round(kFitBaseEditorHeight * 0.25f))));
    setResizeLimits(minEditorWidth, minEditorHeight, maxEditorWidth, maxEditorHeight);
}

std::pair<int, int> PictureWaveSynthAudioProcessorEditor::getIdealEditorSizeForScale(float scale) const
{
    const auto safeScale = juce::jlimit(0.25f, 2.0f, scale);
    const auto width = static_cast<int>(std::round(static_cast<float>(kFitBaseEditorWidth) * safeScale));
    const auto height = static_cast<int>(std::round(static_cast<float>(kFitBaseEditorHeight) * safeScale));
    return {
        juce::jlimit(minEditorWidth, maxEditorWidth, width),
        juce::jlimit(minEditorHeight, maxEditorHeight, height)
    };
}

void PictureWaveSynthAudioProcessorEditor::applyUiZoomSelection(bool resizeWindow)
{
    const auto zoomId = uiZoomCombo.getSelectedId();
    if (zoomId == 1)
        uiScaleFactor = 0.25f;
    else if (zoomId == 2)
        uiScaleFactor = 0.50f;
    else if (zoomId == 3)
        uiScaleFactor = 0.75f;
    else if (zoomId == 4)
        uiScaleFactor = 0.85f;
    else if (zoomId == 5)
        uiScaleFactor = 1.0f;
    else if (zoomId == 6)
        uiScaleFactor = 1.15f;
    else
    {
        auto text = uiZoomCombo.getText().trim();
        text = text.removeCharacters("% ").replaceCharacter(',', '.');
        auto customZoom = text.getDoubleValue();
        if (customZoom > 5.0)
            customZoom *= 0.01;

        uiScaleFactor = juce::jlimit(0.25f, 2.0f, static_cast<float>(customZoom));
        if (uiScaleFactor <= 0.0f)
            uiScaleFactor = 0.75f;
    }

    const auto zoomPercent = static_cast<int>(std::lround(uiScaleFactor * 100.0f));
    uiZoomCombo.setText(juce::String(zoomPercent) + "%", juce::dontSendNotification);

    if (resizeWindow)
    {
        const auto [targetW, targetH] = getIdealEditorSizeForScale(uiScaleFactor);
        setSize(targetW, targetH);
        storeEditorGeometryToState();
    }

    resized();
    updateModeControlLabelsAndVisibility();
    applyGlobalWidgetScale();
}

void PictureWaveSynthAudioProcessorEditor::applyGlobalWidgetScale()
{
    const auto transform = juce::AffineTransform::scale(uiScaleFactor);
    for (int i = 0; i < getNumChildComponents(); ++i)
    {
        if (auto* child = getChildComponent(i))
        {
            if (dynamic_cast<juce::ResizableCornerComponent*>(child) != nullptr
                || dynamic_cast<juce::ResizableBorderComponent*>(child) != nullptr)
            {
                child->setTransform({});
                continue;
            }

            child->setTransform(transform);
        }
    }
}

bool PictureWaveSynthAudioProcessorEditor::restoreEditorGeometryFromState()
{
    auto& state = audioProcessor.parameters.state;
    const auto storedScale = static_cast<float>(state.getProperty("editorUiScale", 0.75f));
    const auto storedWidth = static_cast<int>(state.getProperty("editorWindowWidth", 0));
    const auto storedHeight = static_cast<int>(state.getProperty("editorWindowHeight", 0));

    const auto setZoomSelectionForScale = [this](float scale)
    {
        const auto percent = static_cast<int>(std::lround(scale * 100.0f));
        if (percent == 25)
            uiZoomCombo.setSelectedId(1, juce::dontSendNotification);
        else if (percent == 50)
            uiZoomCombo.setSelectedId(2, juce::dontSendNotification);
        else if (percent == 75)
            uiZoomCombo.setSelectedId(3, juce::dontSendNotification);
        else if (percent == 85)
            uiZoomCombo.setSelectedId(4, juce::dontSendNotification);
        else if (percent == 100)
            uiZoomCombo.setSelectedId(5, juce::dontSendNotification);
        else if (percent == 115)
            uiZoomCombo.setSelectedId(6, juce::dontSendNotification);
        else
            uiZoomCombo.setSelectedId(0, juce::dontSendNotification);

        uiZoomCombo.setText(juce::String(percent) + "%", juce::dontSendNotification);
    };

    if (storedScale > 0.0f)
    {
        uiScaleFactor = juce::jlimit(0.25f, 2.0f, storedScale);
        setZoomSelectionForScale(uiScaleFactor);
    }

    if (storedWidth > 0 && storedHeight > 0)
    {
        setSize(
            juce::jlimit(minEditorWidth, maxEditorWidth, storedWidth),
            juce::jlimit(minEditorHeight, maxEditorHeight, storedHeight));
        return true;
    }

    return false;
}

void PictureWaveSynthAudioProcessorEditor::storeEditorGeometryToState()
{
    auto& state = audioProcessor.parameters.state;
    state.setProperty("editorWindowWidth", getWidth(), nullptr);
    state.setProperty("editorWindowHeight", getHeight(), nullptr);
    state.setProperty("editorUiScale", uiScaleFactor, nullptr);
}

void PictureWaveSynthAudioProcessorEditor::rebuildModeAttachments()
{
    modeCtrl1Attachment.reset();
    modeCtrl2Attachment.reset();
    modeCtrl3Attachment.reset();
    modeCtrl4Attachment.reset();
    modeCtrl5Attachment.reset();
    modeCtrl6Attachment.reset();

    const auto mode = scannerModeCombo.getSelectedId();
    if (mode == 2)
    {
        modeCtrl1Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "ovalX1", shapeCtrl1Slider);
        modeCtrl2Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "ovalY1", shapeCtrl2Slider);
        modeCtrl3Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "ovalX2", shapeCtrl3Slider);
        modeCtrl4Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "ovalY2", shapeCtrl4Slider);
    }
    else if (mode == 3)
    {
        modeCtrl1Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "rectX", shapeCtrl1Slider);
        modeCtrl2Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "rectY", shapeCtrl2Slider);
        modeCtrl3Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "rectWidth", shapeCtrl3Slider);
        modeCtrl4Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "rectHeight", shapeCtrl4Slider);
    }
    else if (mode == 4)
    {
        modeCtrl1Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "triX1", shapeCtrl1Slider);
        modeCtrl2Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "triY1", shapeCtrl2Slider);
        modeCtrl3Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "triX2", shapeCtrl3Slider);
        modeCtrl4Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "triY2", shapeCtrl4Slider);
        modeCtrl5Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "triX3", shapeCtrl5Slider);
        modeCtrl6Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "triY3", shapeCtrl6Slider);
    }
    else if (mode == 5)
    {
        modeCtrl1Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "propX", shapeCtrl1Slider);
        modeCtrl2Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "propY", shapeCtrl2Slider);
        modeCtrl3Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "propSize", shapeCtrl3Slider);

        const auto syncEnabled = propTempoSyncButton.getToggleState();
        if (syncEnabled)
        {
            modeCtrl4Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "propSyncDivision", shapeCtrl4Slider);
        }
        else
        {
            modeCtrl4Attachment = std::make_unique<SliderAttachment>(audioProcessor.parameters, "propSpeed", shapeCtrl4Slider);
        }
    }
}

void PictureWaveSynthAudioProcessorEditor::updateModeControlLabelsAndVisibility()
{
    auto setControlVisibility = [](juce::Slider& slider, juce::Label& label, bool visible)
    {
        slider.setVisible(visible);
        label.setVisible(visible);
    };

    const auto mode = scannerModeCombo.getSelectedId();

    setControlVisibility(shapeCtrl1Slider, shapeCtrl1Label, false);
    setControlVisibility(shapeCtrl2Slider, shapeCtrl2Label, false);
    setControlVisibility(shapeCtrl3Slider, shapeCtrl3Label, false);
    setControlVisibility(shapeCtrl4Slider, shapeCtrl4Label, false);
    setControlVisibility(shapeCtrl5Slider, shapeCtrl5Label, false);
    setControlVisibility(shapeCtrl6Slider, shapeCtrl6Label, false);
    randomPhaseButton.setVisible(false);
    propTempoSyncButton.setVisible(false);

    setControlVisibility(scanXSlider, scanXLabel, false);
    setControlVisibility(scanYSlider, scanYLabel, false);
    setControlVisibility(scanLengthSlider, scanLengthLabel, false);
    setControlVisibility(scanAngleSlider, scanAngleLabel, false);

    if (mode == 1)
    {
        scanXLabel.setText("Line X", juce::dontSendNotification);
        scanYLabel.setText("Line Y", juce::dontSendNotification);
        scanLengthLabel.setText("Line Length", juce::dontSendNotification);
        scanAngleLabel.setText("Line Angle", juce::dontSendNotification);

        setControlVisibility(scanXSlider, scanXLabel, true);
        setControlVisibility(scanYSlider, scanYLabel, true);
        setControlVisibility(scanLengthSlider, scanLengthLabel, true);
        setControlVisibility(scanAngleSlider, scanAngleLabel, true);
    }
    else if (mode == 2)
    {
        shapeCtrl1Label.setText("Oval X1", juce::dontSendNotification);
        shapeCtrl2Label.setText("Oval Y1", juce::dontSendNotification);
        shapeCtrl3Label.setText("Oval X2", juce::dontSendNotification);
        shapeCtrl4Label.setText("Oval Y2", juce::dontSendNotification);

        setControlVisibility(shapeCtrl1Slider, shapeCtrl1Label, true);
        setControlVisibility(shapeCtrl2Slider, shapeCtrl2Label, true);
        setControlVisibility(shapeCtrl3Slider, shapeCtrl3Label, true);
        setControlVisibility(shapeCtrl4Slider, shapeCtrl4Label, true);
    }
    else if (mode == 3)
    {
        shapeCtrl1Label.setText("Rect X", juce::dontSendNotification);
        shapeCtrl2Label.setText("Rect Y", juce::dontSendNotification);
        shapeCtrl3Label.setText("Rect Width", juce::dontSendNotification);
        shapeCtrl4Label.setText("Rect Height", juce::dontSendNotification);

        setControlVisibility(shapeCtrl1Slider, shapeCtrl1Label, true);
        setControlVisibility(shapeCtrl2Slider, shapeCtrl2Label, true);
        setControlVisibility(shapeCtrl3Slider, shapeCtrl3Label, true);
        setControlVisibility(shapeCtrl4Slider, shapeCtrl4Label, true);
    }
    else if (mode == 4)
    {
        shapeCtrl1Label.setText("Tri X1", juce::dontSendNotification);
        shapeCtrl2Label.setText("Tri Y1", juce::dontSendNotification);
        shapeCtrl3Label.setText("Tri X2", juce::dontSendNotification);
        shapeCtrl4Label.setText("Tri Y2", juce::dontSendNotification);
        shapeCtrl5Label.setText("Tri X3", juce::dontSendNotification);
        shapeCtrl6Label.setText("Tri Y3", juce::dontSendNotification);

        setControlVisibility(shapeCtrl1Slider, shapeCtrl1Label, true);
        setControlVisibility(shapeCtrl2Slider, shapeCtrl2Label, true);
        setControlVisibility(shapeCtrl3Slider, shapeCtrl3Label, true);
        setControlVisibility(shapeCtrl4Slider, shapeCtrl4Label, true);
        setControlVisibility(shapeCtrl5Slider, shapeCtrl5Label, true);
        setControlVisibility(shapeCtrl6Slider, shapeCtrl6Label, true);
    }
    else if (mode == 5)
    {
        const auto syncEnabled = propTempoSyncButton.getToggleState();
        shapeCtrl1Label.setText("Prop X", juce::dontSendNotification);
        shapeCtrl2Label.setText("Prop Y", juce::dontSendNotification);
        shapeCtrl3Label.setText("Prop Size", juce::dontSendNotification);
        shapeCtrl4Label.setText(syncEnabled ? "Prop Division" : "Prop Speed", juce::dontSendNotification);

        setControlVisibility(shapeCtrl1Slider, shapeCtrl1Label, true);
        setControlVisibility(shapeCtrl2Slider, shapeCtrl2Label, true);
        setControlVisibility(shapeCtrl3Slider, shapeCtrl3Label, true);
        setControlVisibility(shapeCtrl4Slider, shapeCtrl4Label, true);
        randomPhaseButton.setVisible(true);
        propTempoSyncButton.setVisible(true);

        if (syncEnabled)
        {
            shapeCtrl4Slider.textFromValueFunction = [](double value)
            {
                return syncDivisionTextFromIndex(static_cast<int>(std::lround(value)));
            };
            shapeCtrl4Slider.valueFromTextFunction = [](const juce::String& text)
            {
                for (int i = 0; i < static_cast<int>(kSyncDivisionNames.size()); ++i)
                {
                    if (text.trim().equalsIgnoreCase(kSyncDivisionNames[static_cast<size_t>(i)]))
                    {
                        return static_cast<double>(i);
                    }
                }
                return text.getDoubleValue();
            };
        }
        else
        {
            shapeCtrl4Slider.textFromValueFunction = nullptr;
            shapeCtrl4Slider.valueFromTextFunction = nullptr;
        }
    }

    configureModeSpecificResetBehaviour();
}
