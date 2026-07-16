#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <functional>
#include <utility>

class PictureWaveSynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                  private juce::Timer
{
public:
    explicit PictureWaveSynthAudioProcessorEditor(PictureWaveSynthAudioProcessor&);
    ~PictureWaveSynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    class ImagePreviewComponent final : public juce::Component
    {
    public:
        using ParameterChangeCallback = std::function<void(const juce::String&, float)>;

        void setImage(juce::Image newImage);
        void setScanner(
            int mode,
            float lineX1,
            float lineY1,
            float lineX2,
            float lineY2,
            float ovalX1,
            float ovalY1,
            float ovalX2,
            float ovalY2,
            float rectX,
            float rectY,
            float rectWidth,
            float rectHeight,
            float triX1,
            float triY1,
            float triX2,
            float triY2,
            float triX3,
            float triY3,
            float propX,
            float propY,
            float propSize,
            double propPhase);
        void setParameterChangeCallback(ParameterChangeCallback callback);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseDrag(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;

    private:
        enum class DragHandle
        {
            none,
            lineCenter,
            lineA,
            lineB,
            ovalCenter,
            ovalP1,
            ovalP2,
            rectCenter,
            rectTL,
            rectBR,
            triP1,
            triP2,
            triP3
        };

        juce::Rectangle<float> getImageDrawArea() const;
        juce::Point<float> normalisedToPoint(float u, float v) const;
        std::array<float, 2> pointToNormalised(juce::Point<float> p) const;
        DragHandle hitTestHandle(juce::Point<float> p) const;
        void emitParameter(const juce::String& paramId, float value);
        void dragLineHandle(DragHandle handle, juce::Point<float> p);
        void dragPropHandle(DragHandle handle, juce::Point<float> p);
        void dragOvalHandle(DragHandle handle, juce::Point<float> p);
        void dragRectHandle(DragHandle handle, juce::Point<float> p);
        void dragTriHandle(DragHandle handle, juce::Point<float> p);

        juce::Image image;
        ParameterChangeCallback parameterChangeCallback;
        DragHandle activeHandle = DragHandle::none;
        int scannerMode = 0;
        float lineX1 = 0.2f;
        float lineY1 = 0.5f;
        float lineX2 = 0.8f;
        float lineY2 = 0.5f;
        float ovalX1 = 0.3f;
        float ovalY1 = 0.3f;
        float ovalX2 = 0.7f;
        float ovalY2 = 0.7f;
        float rectX = 0.2f;
        float rectY = 0.2f;
        float rectWidth = 0.4f;
        float rectHeight = 0.3f;
        float triX1 = 0.25f;
        float triY1 = 0.2f;
        float triX2 = 0.75f;
        float triY2 = 0.3f;
        float triX3 = 0.5f;
        float triY3 = 0.8f;
        float propX = 0.5f;
        float propY = 0.5f;
        float propSize = 0.8f;
        double propPhase = 0.0;
    };

    class ModulationSlider final : public juce::Slider
    {
    public:
        void setModulationVisual(float newAmount);
        void setEffectiveNormalisedValue(float newValue);
        void setMappingOverlayEnabled(bool shouldEnable) { mappingOverlayEnabled = shouldEnable; }
        void setOverlayAccentColour(juce::Colour newColour) { overlayAccentColour = newColour; }
        void paintOverChildren(juce::Graphics& g) override;
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    private:
        float modulationAmount = 0.0f;
        float effectiveNormalisedValue = 0.5f;
        bool mappingOverlayEnabled = false;
        juce::Colour overlayAccentColour = juce::Colour::fromRGB(122, 190, 255);
    };

    class LfoVisualizer final : public juce::Component
    {
    public:
        void setVisualState(float newPhase, float newDepth, int newWave, juce::Colour newAccent);
        void paint(juce::Graphics& g) override;

    private:
        static float sampleWave(float phase, int waveType);
        static float hashNoise(int x);

        float phase = 0.0f;
        float depth = 1.0f;
        int wave = 1;
        juce::Colour accent = juce::Colour::fromRGB(122, 174, 235);
    };

    class ScannerWaveformViewer final : public juce::Component
    {
    public:
        explicit ScannerWaveformViewer(juce::Colour newAccent);

        void setWaveform(const PictureWaveSynthAudioProcessor::WaveTable& newSamples);
        void paint(juce::Graphics& g) override;

    private:
        PictureWaveSynthAudioProcessor::WaveTable samples{};
        juce::Colour accent;
    };

    class FilterResponseViewer final : public juce::Component
    {
    public:
        void setFilterState(PictureWaveSynthAudioProcessor::FxFilterSettings newSettings, double newSampleRate);
        void paint(juce::Graphics& g) override;

    private:
        PictureWaveSynthAudioProcessor::FxFilterSettings settings;
        double sampleRate = 44100.0;
    };

    class ResettableComboBox final : public juce::ComboBox
    {
    public:
        using juce::ComboBox::ComboBox;
        void setResetSelectedId(int newSelectedId);
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    private:
        void mouseDoubleClick(const juce::MouseEvent& event) override;
        int resetSelectedId = 0;
    };

    class ResettableToggleButton final : public juce::ToggleButton
    {
    public:
        using juce::ToggleButton::ToggleButton;
        void setResetState(bool newResetState);

    private:
        void mouseDoubleClick(const juce::MouseEvent& event) override;
        bool resetState = false;
    };

    class LfoTabbedComponent final : public juce::TabbedComponent
    {
    public:
        using TabChangedCallback = std::function<void(int, const juce::String&)>;

        explicit LfoTabbedComponent(juce::TabbedButtonBar::Orientation orientation)
            : juce::TabbedComponent(orientation)
        {
        }

        void setTabChangedCallback(TabChangedCallback callback)
        {
            tabChangedCallback = std::move(callback);
        }

    private:
        void currentTabChanged(int newCurrentTabIndex, const juce::String& newCurrentTabName) override
        {
            juce::TabbedComponent::currentTabChanged(newCurrentTabIndex, newCurrentTabName);
            if (tabChangedCallback)
            {
                tabChangedCallback(newCurrentTabIndex, newCurrentTabName);
            }
        }

        TabChangedCallback tabChangedCallback;
    };

    PictureWaveSynthAudioProcessor& audioProcessor;

    ImagePreviewComponent imagePreview;
    juce::TextButton loadImageButton{ "Load Photo" };
    juce::TextButton initButton{ "Init" };
    juce::TextButton loadPresetButton{ "Load Preset" };
    juce::TextButton savePresetButton{ "Save Preset" };
    juce::TextButton aboutButton{ "About" };
    juce::Label imageStatusLabel;

    ModulationSlider scanXSlider;
    ModulationSlider scanYSlider;
    ModulationSlider scanLengthSlider;
    ModulationSlider scanAngleSlider;
    ResettableComboBox scannerModeCombo;
    juce::Label scannerModeLabel;
    ResettableComboBox scanResolutionCombo;
    juce::Label scanResolutionLabel;

    ModulationSlider shapeCtrl1Slider;
    ModulationSlider shapeCtrl2Slider;
    ModulationSlider shapeCtrl3Slider;
    ModulationSlider shapeCtrl4Slider;
    ModulationSlider shapeCtrl5Slider;
    ModulationSlider shapeCtrl6Slider;
    ResettableToggleButton randomPhaseButton{ "Random Propellor Phase" };
    ResettableToggleButton propTempoSyncButton{ "Tempo Sync" };

    juce::Label shapeCtrl1Label;
    juce::Label shapeCtrl2Label;
    juce::Label shapeCtrl3Label;
    juce::Label shapeCtrl4Label;
    juce::Label shapeCtrl5Label;
    juce::Label shapeCtrl6Label;

    ModulationSlider attackSlider;
    ModulationSlider decaySlider;
    ModulationSlider sustainSlider;
    ModulationSlider releaseSlider;
    ModulationSlider gainSlider;
    ModulationSlider noteDriftSlider;
    ModulationSlider liveNoteDriftSlider;
    juce::Label envTypeLabel;
    ResettableComboBox envTypeCombo;

    juce::Label attackLabel;
    juce::Label decayLabel;
    juce::Label sustainLabel;
    juce::Label releaseLabel;
    juce::Label gainLabel;
    juce::Label noteDriftLabel;
    juce::Label liveNoteDriftLabel;

    ModulationSlider mapRLSlider;
    ModulationSlider mapGLSlider;
    ModulationSlider mapBLSlider;
    ModulationSlider mapALSlider;
    ModulationSlider mapRRSlider;
    ModulationSlider mapGRSlider;
    ModulationSlider mapBRSlider;
    ModulationSlider mapARSlider;

    juce::Label titleLabel;
    juce::Label scannerTitleLabel;
    juce::Label mappingTitleLabel;
    ResettableToggleButton scanSplineInterpolationButton{ "Spline" };
    juce::Label envTitleLabel;

    juce::Label scanXLabel;
    juce::Label scanYLabel;
    juce::Label scanLengthLabel;
    juce::Label scanAngleLabel;

    juce::Label mapRLLabel;
    juce::Label mapGLLabel;
    juce::Label mapBLLabel;
    juce::Label mapALLabel;
    juce::Label mapRRLabel;
    juce::Label mapGRLabel;
    juce::Label mapBRLabel;
    juce::Label mapARLabel;

    juce::GroupComponent mapLeftGroup;
    juce::GroupComponent mapRightGroup;
    juce::GroupComponent masterGroup;
    juce::GroupComponent adsrGroup;
    juce::GroupComponent driftGroup;
    ScannerWaveformViewer leftWaveformViewer{ juce::Colour::fromRGB(122, 190, 255) };
    ScannerWaveformViewer rightWaveformViewer{ juce::Colour::fromRGB(255, 166, 102) };

    juce::TabbedComponent scannerTabs{ juce::TabbedButtonBar::TabsAtTop };
    juce::Component photoScannerTabPage;
    juce::Component fxTabPage;
    juce::Component reverbTabPage;
    juce::GroupComponent fxFilterGroup;
    juce::Label fxFilterTypeLabel;
    ResettableComboBox fxFilterTypeCombo;
    ModulationSlider fxFilterCutoffSlider;
    ModulationSlider fxFilterResonanceSlider;
    ModulationSlider fxFilterGainSlider;
    juce::Label fxFilterCutoffLabel;
    juce::Label fxFilterResonanceLabel;
    juce::Label fxFilterGainLabel;
    FilterResponseViewer fxFilterViewer;
    juce::GroupComponent reverbGroup;
    ModulationSlider reverbRoomSizeSlider;
    ModulationSlider reverbDampingSlider;
    ModulationSlider reverbWidthSlider;
    ModulationSlider reverbWetSlider;
    ModulationSlider reverbDrySlider;
    ResettableToggleButton reverbFreezeButton{ "Freeze" };
    juce::Label reverbRoomSizeLabel;
    juce::Label reverbDampingLabel;
    juce::Label reverbWidthLabel;
    juce::Label reverbWetLabel;
    juce::Label reverbDryLabel;

    juce::Label uiZoomLabel;
    ResettableComboBox uiZoomCombo;
    juce::Label polyphonyLabel;
    ResettableComboBox polyphonyCombo;
    juce::Label themeLabel;
    ResettableComboBox themeCombo;
    juce::Label modulationTitleLabel;
    juce::GroupComponent modulationLfoGroup;
    juce::GroupComponent modulationRoutingGroup;
    ModulationSlider modResponseSlider;
    juce::Label modResponseLabel;

    LfoTabbedComponent lfoTabs{ juce::TabbedButtonBar::TabsAtTop };
    juce::Component lfoTabPage;

    juce::TabbedComponent routeTabs{ juce::TabbedButtonBar::TabsAtTop };
    static constexpr int kNumRoutePages = 4;
    static constexpr int kRoutesPerPage = 8;
    std::array<juce::Component, kNumRoutePages> routeTabPages;
    juce::Component routeSettingsTabPage;
    std::array<juce::Label, kNumRoutePages> routeColumnHeaderSource;
    std::array<juce::Label, kNumRoutePages> routeColumnHeaderDestination;
    std::array<juce::Label, kNumRoutePages> routeColumnHeaderBipolar;
    std::array<juce::Label, kNumRoutePages> routeColumnHeaderAmount;

    ModulationSlider lfoRateSlider;
    ModulationSlider lfoDepthSlider;
    ResettableComboBox lfoWaveCombo;
    ResettableToggleButton lfoSyncButton{ "Tempo Sync" };
    ResettableToggleButton lfoRandomPhasePerVoiceButton{ "Random Phase Per Voice" };
    juce::Label lfoRateLabel;
    juce::Label lfoDepthLabel;
    juce::Label lfoWaveLabel;
    LfoVisualizer lfoVisualizer;
    float lfoVisualizerPhase = 0.0f;

    static constexpr int kNumModRows = 32;
    std::array<ResettableToggleButton, kNumModRows> modEnabledButtons{};
    std::array<ResettableComboBox, kNumModRows> modSourceCombos;
    std::array<ResettableComboBox, kNumModRows> modTargetCombos;
    std::array<ResettableToggleButton, kNumModRows> modBipolarButtons{};
    std::array<ModulationSlider, kNumModRows> modAmountSliders{};
    std::array<juce::Label, kNumModRows> modRowLabels{};

    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> decayAttachment;
    std::unique_ptr<SliderAttachment> sustainAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> noteDriftAttachment;
    std::unique_ptr<SliderAttachment> liveNoteDriftAttachment;
    std::unique_ptr<ComboBoxAttachment> envTypeAttachment;

    std::unique_ptr<SliderAttachment> scanXAttachment;
    std::unique_ptr<SliderAttachment> scanYAttachment;
    std::unique_ptr<SliderAttachment> scanLengthAttachment;
    std::unique_ptr<SliderAttachment> scanAngleAttachment;
    std::unique_ptr<SliderAttachment> modeCtrl1Attachment;
    std::unique_ptr<SliderAttachment> modeCtrl2Attachment;
    std::unique_ptr<SliderAttachment> modeCtrl3Attachment;
    std::unique_ptr<SliderAttachment> modeCtrl4Attachment;
    std::unique_ptr<SliderAttachment> modeCtrl5Attachment;
    std::unique_ptr<SliderAttachment> modeCtrl6Attachment;
    std::unique_ptr<ComboBoxAttachment> scannerModeAttachment;
    std::unique_ptr<ComboBoxAttachment> scanResolutionAttachment;
    std::unique_ptr<ButtonAttachment> scanSplineInterpolationAttachment;
    std::unique_ptr<ButtonAttachment> randomPhaseAttachment;
    std::unique_ptr<ButtonAttachment> propTempoSyncAttachment;

    std::unique_ptr<SliderAttachment> mapRLAttachment;
    std::unique_ptr<SliderAttachment> mapGLAttachment;
    std::unique_ptr<SliderAttachment> mapBLAttachment;
    std::unique_ptr<SliderAttachment> mapALAttachment;
    std::unique_ptr<SliderAttachment> mapRRAttachment;
    std::unique_ptr<SliderAttachment> mapGRAttachment;
    std::unique_ptr<SliderAttachment> mapBRAttachment;
    std::unique_ptr<SliderAttachment> mapARAttachment;
    std::unique_ptr<ComboBoxAttachment> fxFilterTypeAttachment;
    std::unique_ptr<SliderAttachment> fxFilterCutoffAttachment;
    std::unique_ptr<SliderAttachment> fxFilterResonanceAttachment;
    std::unique_ptr<SliderAttachment> fxFilterGainAttachment;
    std::unique_ptr<SliderAttachment> reverbRoomSizeAttachment;
    std::unique_ptr<SliderAttachment> reverbDampingAttachment;
    std::unique_ptr<SliderAttachment> reverbWidthAttachment;
    std::unique_ptr<SliderAttachment> reverbWetAttachment;
    std::unique_ptr<SliderAttachment> reverbDryAttachment;
    std::unique_ptr<ButtonAttachment> reverbFreezeAttachment;

    std::unique_ptr<SliderAttachment> activeLfoRateAttachment;
    std::unique_ptr<SliderAttachment> activeLfoDepthAttachment;
    std::unique_ptr<ComboBoxAttachment> activeLfoWaveAttachment;
    std::unique_ptr<ButtonAttachment> activeLfoSyncAttachment;
    std::unique_ptr<ButtonAttachment> activeLfoRandomPhasePerVoiceAttachment;
    std::unique_ptr<ComboBoxAttachment> polyphonyAttachment;

    std::array<std::unique_ptr<ButtonAttachment>, kNumModRows> modEnabledAttachments;
    std::array<std::unique_ptr<ComboBoxAttachment>, kNumModRows> modSourceAttachments;
    std::array<std::unique_ptr<ComboBoxAttachment>, kNumModRows> modTargetAttachments;
    std::array<std::unique_ptr<ButtonAttachment>, kNumModRows> modBipolarAttachments;
    std::array<std::unique_ptr<SliderAttachment>, kNumModRows> modAmountAttachments;
    std::unique_ptr<SliderAttachment> modResponseAttachment;
    int lastLfoTabIndex = -1;

    std::unique_ptr<juce::FileChooser> imageChooser;
    std::unique_ptr<juce::FileChooser> presetChooser;

    void setupRotarySlider(juce::Slider& slider, const juce::String& name);
    void setupLinearSlider(juce::Slider& slider, const juce::String& name);
    void setupBipolarSlider(juce::Slider& slider, const juce::String& name);
    void setupSmallLabel(juce::Label& label, const juce::String& text);
    void setupSectionLabel(juce::Label& label, const juce::String& text);
    void setupScannerSlider(juce::Slider& slider, juce::Label& label, const juce::String& name);
    void setupMappingSlider(juce::Slider& slider, juce::Label& label, const juce::String& name);
    void setupModeSpecificSlider(juce::Slider& slider, juce::Label& label);
    void configureResetBehaviour();
    void configureModeSpecificResetBehaviour();
    void configureSliderReset(juce::Slider& slider, const char* paramId);
    void configureComboReset(ResettableComboBox& combo, const char* paramId);
    void configureToggleReset(ResettableToggleButton& button, const char* paramId);
    void openImageChooser();
    void savePresetToFile();
    void loadPresetFromFile();
    void updateParameterFromPreview(const juce::String& paramId, float value);
    void refreshImagePreview();
    void rebuildModeAttachments();
    void rebuildActiveLfoAttachments();
    void updateResizeLimitsForDisplay();
    void applyUiZoomSelection(bool resizeWindow = true);
    void applyGlobalWidgetScale();
    void refreshModulationVisuals();
    void refreshWaveformViewers();
    void refreshFxFilterViewer();
    void updateFxFilterControlState();
    void updateEnvelopeControlState();
    void updateModeControlLabelsAndVisibility();
    void applyThemeSelection(bool storeState = true);
    void applyThemeToComponents();
    bool restoreEditorGeometryFromState();
    void storeEditorGeometryToState();
    std::pair<int, int> getIdealEditorSizeForScale(float scale) const;
    void timerCallback() override;

    static constexpr int kBaseEditorWidth = 1240;
    static constexpr int kBaseEditorHeight = 980;
    static constexpr int kFitBaseEditorWidth = 1230;
    static constexpr int kFitBaseEditorHeight = 1120;
    float uiScaleFactor = 0.75f;
    int minEditorWidth = 720;
    int minEditorHeight = 520;
    int maxEditorWidth = 1800;
    int maxEditorHeight = 1400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PictureWaveSynthAudioProcessorEditor)
};
