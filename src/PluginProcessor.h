#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <vector>

namespace
{
constexpr int kInternalWaveTableSize = 2048;
}

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
    void setWaveTables(const float* leftTable, const float* rightTable, int size, uint32_t generation);
    void setNoteDriftAmount(float amount);
    void setLiveNoteDriftRateHz(float rateHz);
    void setRandomSeed(uint32_t seed);
    double getPropellorPhaseOffset() const;
    uint32_t getRandomModulationSeed() const;
    float getNoteOnVelocity() const;
    void forceStop();

private:
    int waveTableSize = 0;
    std::array<float, kInternalWaveTableSize> currentWaveTableLeft{};
    std::array<float, kInternalWaveTableSize> currentWaveTableRight{};
    std::array<float, kInternalWaveTableSize> targetWaveTableLeft{};
    std::array<float, kInternalWaveTableSize> targetWaveTableRight{};
    std::array<float, kInternalWaveTableSize> pendingWaveTableLeft{};
    std::array<float, kInternalWaveTableSize> pendingWaveTableRight{};
    int waveTableFadeSamples = 1;
    int waveTableFadeSamplesRemaining = 0;
    int pendingWaveTableSize = 0;
    uint32_t appliedWaveTableGeneration = std::numeric_limits<uint32_t>::max();
    uint32_t pendingWaveTableGeneration = std::numeric_limits<uint32_t>::max();
    bool hasWaveTable = false;
    bool hasPendingWaveTable = false;

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
    juce::Random randomGenerator;
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
    static constexpr int kNumModTargets = 39;
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
        float lineX1 = 0.2f;
        float lineY1 = 0.5f;
        float lineX2 = 0.8f;
        float lineY2 = 0.5f;
        float ovalX1 = 0.3f;
        float ovalY1 = 0.3f;
        float ovalX2 = 0.7f;
        float ovalY2 = 0.7f;
        float ovalRotation = 0.0f;
        float rectX = 0.2f;
        float rectY = 0.2f;
        float rectWidth = 0.4f;
        float rectHeight = 0.3f;
        float rectRotation = 0.0f;
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
        bool useSplineInterpolation = false;
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
    void cacheParameterPointers();
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
    uint32_t waveTableGeneration = 0;
    WaveTable previewWaveTableLeft{};
    WaveTable previewWaveTableRight{};
    std::vector<std::array<float, kWaveTableSize>> perVoiceWaveTableLeft;
    std::vector<std::array<float, kWaveTableSize>> perVoiceWaveTableRight;
    std::vector<uint32_t> perVoiceWaveTableGenerations;
    std::vector<std::array<float, kNumModTargets>> perVoiceSmoothedModulationSums;
    std::vector<ScannerParams> perVoiceLastScannerParams;
    std::vector<double> perVoiceLastPropellorPhase;
    std::vector<bool> perVoiceHasCachedScannerState;
    std::array<double, 8> lfoPhases{};
    std::array<int64_t, 8> lfoCyclePositions{};

    struct ParameterCache
    {
        std::atomic<float>* maxVoices = nullptr;
        std::atomic<float>* modResponseMs = nullptr;
        std::atomic<float>* envType = nullptr;
        std::atomic<float>* scanResolution = nullptr;
        std::atomic<float>* scanSplineInterpolation = nullptr;
        std::atomic<float>* scannerMode = nullptr;
        std::atomic<float>* propSyncDivision = nullptr;
        std::atomic<float>* propTempoSync = nullptr;
        std::atomic<float>* randomPhase = nullptr;

        std::atomic<float>* fxFilterType = nullptr;
        std::atomic<float>* fxFilterCutoff = nullptr;
        std::atomic<float>* fxFilterResonance = nullptr;
        std::atomic<float>* fxFilterGain = nullptr;

        std::atomic<float>* reverbRoomSize = nullptr;
        std::atomic<float>* reverbDamping = nullptr;
        std::atomic<float>* reverbWidth = nullptr;
        std::atomic<float>* reverbWet = nullptr;
        std::atomic<float>* reverbDry = nullptr;
        std::atomic<float>* reverbFreeze = nullptr;

        std::array<std::atomic<float>*, 8> lfoRate{};
        std::array<std::atomic<float>*, 8> lfoDepth{};
        std::array<std::atomic<float>*, 8> lfoWave{};
        std::array<std::atomic<float>*, 8> lfoSync{};
        std::array<std::atomic<float>*, 8> lfoDivision{};
        std::array<std::atomic<float>*, 8> lfoRandomPhasePerVoice{};

        std::array<std::atomic<float>*, 32> modEnabled{};
        std::array<std::atomic<float>*, 32> modSource{};
        std::array<std::atomic<float>*, 32> modTarget{};
        std::array<std::atomic<float>*, 32> modAmount{};
        std::array<std::atomic<float>*, 32> modBipolar{};

        std::array<juce::RangedAudioParameter*, kNumModTargets> modTargetParams{};
        std::array<std::atomic<float>*, kNumModTargets> modTargetRaw{};
    } paramCache;

    juce::Random instanceRandom;
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

    FxFilterSettings lastFxSettings{};
    ReverbSettings lastReverbSettings{};
    bool hasLastFxSettings = false;
    bool hasLastReverbSettings = false;
    double lastFxSampleRate = 0.0;
    double lastDcSampleRate = 0.0;
    bool hasLastDcSampleRate = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PictureWaveSynthAudioProcessor)
};
