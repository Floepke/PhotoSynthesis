#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class SineWaveSound final : public juce::SynthesiserSound
{
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

enum class EnvelopeMode
{
    adsr = 0,
    asr,
    ar
};

class SineWaveVoice final : public juce::SynthesiserVoice
{
public:
    using juce::SynthesiserVoice::renderNextBlock;

    void prepare(double sampleRate, int samplesPerBlock, int outputChannels);
    bool canPlaySound(juce::SynthesiserSound* sound) override;
    void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote(float velocity, bool allowTailOff) override;
    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

    void setRandomPropellorPhaseEnabled(bool enabled);

    void renderNextBlock(juce::AudioBuffer<float>&, int startSample, int numSamples) override;

    void setAdsrSampleRate(double sampleRate);
    void updateAdsr(float attackMs, float decayMs, float sustainLevel, float releaseMs, EnvelopeMode mode);
    void setWaveTables(const float* leftTable, const float* rightTable, int size);
    void setNoteDriftAmount(float amount);
    void setLiveNoteDriftRateHz(float rateHz);
    double getPropellorPhaseOffset() const;
    uint32_t getRandomModulationSeed() const;
    float getNoteOnVelocity() const;
    void forceStop();

private:
    const float* waveTableLeft = nullptr;
    const float* waveTableRight = nullptr;
    int waveTableSize = 0;

    juce::ADSR adsr;
    juce::ADSR::Parameters adsrParams;
    double currentSampleRate = 44100.0;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float level = 0.0f;
    float noteDriftAmount = 0.05f;
    float liveNoteDriftRateHz = 1.0f;
    float driftCurrentSemitone = 0.0f;
    float driftStartSemitone = 0.0f;
    float driftTargetSemitone = 0.0f;
    int driftSegmentLengthSamples = 0;
    int driftSegmentProgress = 0;
    float propellorPhaseOffset = 0.0f;
    uint32_t randomModulationSeed = 0;
    float noteOnVelocity = 0.0f;
    float envelopeOutputLevel = 1.0f;
    int arReleaseSampleCountdown = 0;
    EnvelopeMode envelopeMode = EnvelopeMode::adsr;
    bool randomPropellorPhaseEnabled = false;
    bool arReleaseTriggered = false;
    bool retriggeringFromSteal = false;
};

class RoundRobinSynthesiser final : public juce::Synthesiser
{
public:
    void setActiveVoiceLimit(int newLimit);

protected:
    juce::SynthesiserVoice* findFreeVoice(juce::SynthesiserSound* soundToPlay,
                                          int midiChannel,
                                          int midiNoteNumber,
                                          bool stealIfNoneAvailable) const override;
    juce::SynthesiserVoice* findVoiceToSteal(juce::SynthesiserSound* soundToPlay,
                                             int midiChannel,
                                             int midiNoteNumber) const override;

private:
    mutable int nextVoiceIndex = 0;
    mutable int activeVoiceLimit = 0;
};

class PictureWaveSynthAudioProcessor final : public juce::AudioProcessor
{
public:
    using WaveTable = std::array<float, 2048>;
    using IIRCoefficientsPtr = juce::dsp::IIR::Coefficients<float>::Ptr;

    using juce::AudioProcessor::processBlock;

    struct FxFilterSettings
    {
        int type = 0;
        float cutoffHz = 1000.0f;
        float resonance = 0.707f;
        float gainDecibels = 0.0f;
    };

    struct ReverbSettings
    {
        float roomSize = 0.35f;
        float damping = 0.5f;
        float width = 1.0f;
        float wetLevel = 0.0f;
        float dryLevel = 1.0f;
        float freezeMode = 0.0f;
    };

    PictureWaveSynthAudioProcessor();
    ~PictureWaveSynthAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    void resetToInitialPreset();

    using ParameterLayout = juce::AudioProcessorValueTreeState::ParameterLayout;
    static ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;

    bool loadImageFromFile(const juce::File& imageFile, juce::String& errorMessage);
    juce::Image getLoadedImageCopy() const;
    bool hasLoadedImage() const;
    double getPropellorPhase() const;
    float getModulationAmountForParameter(const char* paramId) const;
    float getEffectiveParameterValue(const char* paramId) const;
    void copyCurrentWaveTablePreview(WaveTable& left, WaveTable& right) const;
    FxFilterSettings getFxFilterSettings() const;
    ReverbSettings getReverbSettings() const;
    static juce::StringArray getFxFilterTypeNames();
    static bool fxFilterTypeUsesGain(int type);
    static IIRCoefficientsPtr createFxFilterCoefficients(const FxFilterSettings& settings, double sampleRate);
    static IIRCoefficientsPtr createDcBlockerCoefficients(double sampleRate);

private:
    static constexpr int kNumModTargets = 37;
    using StereoIIRFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    struct LoadedImageData
    {
        int width = 0;
        int height = 0;
        juce::Image image;
        std::vector<juce::Colour> pixels;
    };

    struct ScannerParams
    {
        int mode = 0;
        float x = 0.5f;
        float y = 0.5f;
        float length = 0.7f;
        float angleDegrees = 0.0f;
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
        float propSpeed = 0.0f;
        int scanResolution = 3;
        int propSyncDivision = 2;
        bool propTempoSync = false;
        float mapRL = 1.0f;
        float mapGL = 0.0f;
        float mapBL = 0.0f;
        float mapAL = 0.0f;
        float mapRR = 0.0f;
        float mapGR = 1.0f;
        float mapBR = 0.0f;
        float mapAR = 0.0f;
    };

    static constexpr int kWaveTableSize = 2048;

    bool applyLoadedImage(const juce::Image& image, juce::String& errorMessage);
    void clearLoadedImage();
    void updateWaveTablePreview(const float* left, const float* right);
    void updateDcBlocker();
    void updateFxFilter();
    void updateReverb();

    void regenerateWaveTablesIfNeeded(const ScannerParams& scanner);
    void regenerateWaveTablesFromImage(const LoadedImageData& imageData,
                                       const ScannerParams& scanner,
                                       double propellorPhaseValue,
                                       float* outLeft,
                                       float* outRight);
    void generateFallbackWaveTables(float* outLeft, float* outRight);
    static std::array<float, 2> sampleScannerPoint(const ScannerParams& scanner, float t, double propellorPhase);
    static std::array<float, 4> sampleImageBilinear(const LoadedImageData& imageData, float u, float v);
    static bool scannerParamsEqual(const ScannerParams& a, const ScannerParams& b);

    RoundRobinSynthesiser synth;
    StereoIIRFilter dcBlocker;
    StereoIIRFilter fxFilter;
    juce::Reverb reverb;
    mutable juce::SpinLock imageLock;
    mutable juce::SpinLock waveTablePreviewLock;
    std::shared_ptr<LoadedImageData> loadedImageData;
    std::vector<SineWaveVoice*> voices;
    std::array<float, kWaveTableSize> waveTableLeft{};
    std::array<float, kWaveTableSize> waveTableRight{};
    WaveTable previewWaveTableLeft{};
    WaveTable previewWaveTableRight{};
    std::vector<std::array<float, kWaveTableSize>> perVoiceWaveTableLeft;
    std::vector<std::array<float, kWaveTableSize>> perVoiceWaveTableRight;
    std::vector<std::array<float, kNumModTargets>> perVoiceSmoothedModulationSums;
    std::vector<ScannerParams> perVoiceLastScannerParams;
    std::vector<double> perVoiceLastPropellorPhase;
    std::vector<bool> perVoiceHasCachedScannerState;
    std::array<double, 8> lfoPhases{};
    std::array<int64_t, 8> lfoCyclePositions{};
    std::array<std::atomic<float>, kNumModTargets> modulationDisplayValues{};
    std::array<std::atomic<float>, kNumModTargets> effectiveDisplayValues{};
    std::array<float, kNumModTargets> smoothedModulationSums{};
    std::array<float, kNumModTargets> smoothedPreviewModulationSums{};
    std::atomic<bool> waveTableDirty{ true };
    std::atomic<double> propellorPhase { 0.0 };
    juce::MemoryBlock initialStateWithoutImage;
    ScannerParams lastScannerParams;
    bool hasLastScannerParams = false;
    int modulationPreviewVoiceIndex = -1;
    uint32_t modulationPreviewSeed = 0;
    float modulationVelocity = 0.0f;
    float modulationAftertouch = 0.0f;
    float modulationModWheel = 0.0f;
    int heldNoteCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PictureWaveSynthAudioProcessor)
};
