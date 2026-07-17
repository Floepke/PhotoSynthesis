#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
constexpr int kMaxVoices = 64;
constexpr const char* kEmbeddedImageTag = "EMBEDDED_IMAGE";
constexpr const char* kEmbeddedImageEncoding = "base64-png";
constexpr uint32_t kStateMagic = 0x50575331; // PWS1
constexpr uint32_t kStateVersion = 1;
constexpr int kNumLfos = 8;
constexpr int kNumModRoutes = 32;
constexpr int kNumStandardModSources = 4;
constexpr int kNumModSources = kNumLfos + kNumStandardModSources;

enum StandardModSource
{
    velocity = kNumLfos,
    aftertouch,
    modWheel,
    noteGate
};

const std::array<const char*, 9> kSyncDivisionNames{ "1/1", "1/2", "1/4", "1/8", "1/16", "1/2T", "1/4T", "1/8T", "1/16T" };
const std::array<double, 9> kSyncDivisionBeatsPerCycle{ 4.0, 2.0, 1.0, 0.5, 0.25, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
const std::array<int, 7> kScanResolutionValues{ 32, 64, 128, 256, 512, 1024, 2048 };

const std::array<const char*, 39> kModTargetParamIds{
    "attack", "decay", "sustain", "release", "gain", "noteDrift", "liveNoteDrift",
    "lineX1", "lineY1", "lineX2", "lineY2",
    "ovalX1", "ovalY1", "ovalX2", "ovalY2", "ovalRotation",
    "rectX", "rectY", "rectWidth", "rectHeight", "rectRotation",
    "triX1", "triY1", "triX2", "triY2", "triX3", "triY3",
    "propX", "propY", "propSize", "propSpeed",
    "mapRL", "mapGL", "mapBL", "mapAL", "mapRR", "mapGR", "mapBR", "mapAR"
};

const std::array<const char*, 40> kModTargetNames{
    "None",
    "Attack", "Decay", "Sustain", "Release", "Gain", "Note Drift", "Drift Freq",
    "Line X1", "Line Y1", "Line X2", "Line Y2", 
    "Oval X1", "Oval Y1", "Oval X2", "Oval Y2", "Oval Rot",
    "Rect X", "Rect Y", "Rect Width", "Rect Height", "Rect Rot",
    "Tri X1", "Tri Y1", "Tri X2", "Tri Y2", "Tri X3", "Tri Y3",
    "Prop X", "Prop Y", "Prop Size", "Prop Speed",
    "R->L", "G->L", "B->L", "A->L", "R->R", "G->R", "B->R", "A->R"
};

enum ModTargetIndex
{
    mtAttack = 0,
    mtDecay,
    mtSustain,
    mtRelease,
    mtGain,
    mtNoteDrift,
    mtLiveNoteDrift,
    mtLineX1,
    mtLineY1,
    mtLineX2,
    mtLineY2,
    mtOvalX1,
    mtOvalY1,
    mtOvalX2,
    mtOvalY2,
    mtOvalRotation,
    mtRectX,
    mtRectY,
    mtRectWidth,
    mtRectHeight,
    mtRectRotation,
    mtTriX1,
    mtTriY1,
    mtTriX2,
    mtTriY2,
    mtTriX3,
    mtTriY3,
    mtPropX,
    mtPropY,
    mtPropSize,
    mtPropSpeed,
    mtMapRL,
    mtMapGL,
    mtMapBL,
    mtMapAL,
    mtMapRR,
    mtMapGR,
    mtMapBR,
    mtMapAR
};

template <typename T>
T linearInterpolate(T a, T b, T t)
{
    return a + (b - a) * t;
}

template <typename T>
T catmullRomInterpolate(T p0, T p1, T p2, T p3, T t)
{
    const auto t2 = t * t;
    const auto t3 = t2 * t;
    return static_cast<T>(0.5) * ((static_cast<T>(2) * p1)
        + (-p0 + p2) * t
        + (static_cast<T>(2) * p0 - static_cast<T>(5) * p1 + static_cast<T>(4) * p2 - p3) * t2
        + (-p0 + static_cast<T>(3) * p1 - static_cast<T>(3) * p2 + p3) * t3);
}

double propellorDivisionToBeatsPerCycle(int division)
{
    const auto clamped = static_cast<size_t>(juce::jlimit(0, static_cast<int>(kSyncDivisionBeatsPerCycle.size()) - 1, division));
    return kSyncDivisionBeatsPerCycle[clamped];
}

float renderLfoSample(int waveform, double phase)
{
    const auto wrapped = phase - std::floor(phase);
    switch (juce::jlimit(0, 3, waveform))
    {
        case 0: return std::sin(juce::MathConstants<double>::twoPi * wrapped);
        case 1: return static_cast<float>(4.0 * std::abs(wrapped - 0.5) - 1.0);
        case 2: return static_cast<float>(2.0 * wrapped - 1.0);
        case 3: return wrapped < 0.5 ? 1.0f : -1.0f;
        default: return 0.0f;
    }
}

bool isRandomLfoWaveform(int waveform)
{
    return waveform >= 4;
}

uint32_t mixNoiseSeed(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

float randomBipolarValue(int lfoIndex, int64_t stepIndex, uint32_t voiceSeed)
{
    const auto seed = static_cast<uint32_t>(stepIndex)
        ^ (static_cast<uint32_t>(stepIndex >> 32) * 0x9e3779b9U)
        ^ (static_cast<uint32_t>(lfoIndex + 1) * 0x85ebca6bU)
        ^ (voiceSeed * 0xc2b2ae35U);
    const auto hashed = mixNoiseSeed(seed);
    return static_cast<float>(static_cast<double>(hashed) / static_cast<double>(std::numeric_limits<uint32_t>::max()) * 2.0 - 1.0);
}

float smootherStep(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float renderExtendedLfoSample(int waveform, int lfoIndex, int64_t cyclePosition, double phase, uint32_t voiceSeed)
{
    if (waveform <= 3)
    {
        return renderLfoSample(waveform, phase);
    }

    const auto wrapped = static_cast<float>(phase - std::floor(phase));
    const auto current = randomBipolarValue(lfoIndex, cyclePosition, voiceSeed);
    const auto next = randomBipolarValue(lfoIndex, cyclePosition + 1, voiceSeed);

    switch (juce::jlimit(4, 6, waveform))
    {
        case 4:
            return current;
        case 5:
            return linearInterpolate(current, next, wrapped);
        case 6:
            return linearInterpolate(current, next, smootherStep(wrapped));
        default:
            return current;
    }
}

uint32_t makeUniqueSeed(const void* instanceAddress, uint32_t salt = 0)
{
    static std::atomic<uint32_t> seedCounter{ 1u };
    const auto counter = seedCounter.fetch_add(1u, std::memory_order_relaxed);
    const auto ticks = static_cast<uint64_t>(juce::Time::getHighResolutionTicks());
    const auto ptr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(instanceAddress));
    const auto mixed = static_cast<uint32_t>(ticks)
        ^ static_cast<uint32_t>(ticks >> 32)
        ^ static_cast<uint32_t>(ptr)
        ^ static_cast<uint32_t>(ptr >> 32)
        ^ counter
        ^ salt;
    return mixNoiseSeed(mixed == 0u ? 0x9e3779b9u : mixed);
}

double phaseOffsetFromVoiceSeed(int lfoIndex, uint32_t voiceSeed)
{
    const auto mixedSeed = mixNoiseSeed(voiceSeed ^ (0x9e3779b9U * static_cast<uint32_t>(lfoIndex + 1)));
    return static_cast<double>(mixedSeed) / static_cast<double>(std::numeric_limits<uint32_t>::max());
}

int modTargetIndexForParamId(const char* paramId)
{
    for (int i = 0; i < static_cast<int>(kModTargetParamIds.size()); ++i)
    {
        if (std::strcmp(kModTargetParamIds[static_cast<size_t>(i)], paramId) == 0)
        {
            return i;
        }
    }

    return -1;
}

std::array<float, 2> rotatePointAroundCenter(float x, float y, float cx, float cy, float angleRadians)
{
    const auto dx = x - cx;
    const auto dy = y - cy;
    const auto cosA = std::cos(angleRadians);
    const auto sinA = std::sin(angleRadians);
    return {
        cx + dx * cosA - dy * sinA,
        cy + dx * sinA + dy * cosA
    };
}

enum FxFilterType
{
    fxFilterOff = 0,
    fxFilterLowPass,
    fxFilterHighPass,
    fxFilterBandPass,
    fxFilterNotch,
    fxFilterPeak,
    fxFilterLowShelf,
    fxFilterHighShelf,
    fxFilterAllPass
};

bool fxSettingsEqual(const PictureWaveSynthAudioProcessor::FxFilterSettings& a,
                     const PictureWaveSynthAudioProcessor::FxFilterSettings& b)
{
    return a.type == b.type
        && juce::approximatelyEqual(a.cutoffHz, b.cutoffHz)
        && juce::approximatelyEqual(a.resonance, b.resonance)
        && juce::approximatelyEqual(a.gainDecibels, b.gainDecibels);
}

bool reverbSettingsEqual(const PictureWaveSynthAudioProcessor::ReverbSettings& a,
                         const PictureWaveSynthAudioProcessor::ReverbSettings& b)
{
    return juce::approximatelyEqual(a.roomSize, b.roomSize)
        && juce::approximatelyEqual(a.damping, b.damping)
        && juce::approximatelyEqual(a.width, b.width)
        && juce::approximatelyEqual(a.wetLevel, b.wetLevel)
        && juce::approximatelyEqual(a.dryLevel, b.dryLevel)
        && juce::approximatelyEqual(a.freezeMode, b.freezeMode);
}
}

void SineWaveVoice::prepare(double sampleRate, int samplesPerBlock, int outputChannels)
{
    juce::ignoreUnused(samplesPerBlock, outputChannels);
    currentSampleRate = sampleRate;
    waveTableFadeSamples = juce::jmax(1, static_cast<int>(std::round(currentSampleRate * 0.020)));
    waveTableFadeSamplesRemaining = 0;
    hasPendingWaveTable = false;
}

bool SineWaveVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SineWaveSound*>(sound) != nullptr;
}

void SineWaveVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    noteOnVelocity = juce::jlimit(0.0f, 1.0f, velocity);
    level = velocity * 0.2f;
    const auto baseFrequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    phaseDelta = baseFrequency / currentSampleRate;

    const auto maxSemitoneOffset = juce::jlimit(0.0f, 1.0f, noteDriftAmount);
    const auto nextDriftValue = [this, maxSemitoneOffset]()
    {
        return randomGenerator.nextFloat() * 2.0f * maxSemitoneOffset - maxSemitoneOffset;
    };

    if (maxSemitoneOffset <= 0.0f)
    {
        driftCurrentSemitone = 0.0f;
        driftStartSemitone = 0.0f;
        driftTargetSemitone = 0.0f;
        driftSegmentLengthSamples = 0;
        driftSegmentProgress = 0;
    }
    else if (liveNoteDriftRateHz <= 0.0f)
    {
        driftCurrentSemitone = nextDriftValue();
        driftStartSemitone = driftCurrentSemitone;
        driftTargetSemitone = driftCurrentSemitone;
        driftSegmentLengthSamples = 0;
        driftSegmentProgress = 0;
    }
    else
    {
        driftCurrentSemitone = nextDriftValue();
        driftStartSemitone = driftCurrentSemitone;
        driftTargetSemitone = nextDriftValue();
        driftSegmentLengthSamples = juce::jmax(1, static_cast<int>(std::round(currentSampleRate / static_cast<double>(liveNoteDriftRateHz))));
        driftSegmentProgress = 0;
    }

    if (! retriggeringFromSteal)
    {
        phase = 0.0;
    }

    retriggeringFromSteal = false;
    randomModulationSeed = static_cast<uint32_t>(randomGenerator.nextInt());
    propellorPhaseOffset = randomPropellorPhaseEnabled ? randomGenerator.nextFloat() : 0.0f;
    arReleaseTriggered = false;
    arReleaseSampleCountdown = juce::jmax(1, arReleaseSampleCountdown);
    adsr.noteOn();
}

void SineWaveVoice::setRandomPropellorPhaseEnabled(bool enabled)
{
    randomPropellorPhaseEnabled = enabled;
}

void SineWaveVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
        retriggeringFromSteal = false;
    }
    else
    {
        adsr.noteOff();
        retriggeringFromSteal = true;
    }
}

void SineWaveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!isVoiceActive() || !hasWaveTable || waveTableSize < 2)
    {
        return;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (noteDriftAmount > 0.0f)
        {
            if (liveNoteDriftRateHz > 0.0f)
            {
                driftSegmentLengthSamples = juce::jmax(1, static_cast<int>(std::round(currentSampleRate / static_cast<double>(liveNoteDriftRateHz))));

                const auto t = static_cast<float>(driftSegmentProgress) / static_cast<float>(driftSegmentLengthSamples);
                driftCurrentSemitone = linearInterpolate(driftStartSemitone, driftTargetSemitone, juce::jlimit(0.0f, 1.0f, t));

                ++driftSegmentProgress;
                if (driftSegmentProgress >= driftSegmentLengthSamples)
                {
                    const auto maxSemitoneOffset = juce::jlimit(0.0f, 1.0f, noteDriftAmount);
                    const auto nextTarget = randomGenerator.nextFloat() * 2.0f * maxSemitoneOffset - maxSemitoneOffset;
                    driftStartSemitone = driftCurrentSemitone;
                    driftTargetSemitone = nextTarget;
                    driftSegmentProgress = 0;
                }
            }
        }
        else
        {
            driftCurrentSemitone = 0.0f;
        }

        auto wrappedPhase = phase + static_cast<double>(propellorPhaseOffset);
        wrappedPhase -= std::floor(wrappedPhase);
        const auto tablePosition = wrappedPhase * static_cast<double>(waveTableSize);
        const auto indexA = static_cast<int>(tablePosition) % waveTableSize;
        const auto indexB = (indexA + 1) % waveTableSize;
        const auto frac = static_cast<float>(tablePosition - static_cast<double>(indexA));

        auto leftSample = linearInterpolate(currentWaveTableLeft[static_cast<size_t>(indexA)], currentWaveTableLeft[static_cast<size_t>(indexB)], frac);
        auto rightSample = linearInterpolate(currentWaveTableRight[static_cast<size_t>(indexA)], currentWaveTableRight[static_cast<size_t>(indexB)], frac);

        if (waveTableFadeSamplesRemaining > 0)
        {
            const auto fade = 1.0f - (static_cast<float>(waveTableFadeSamplesRemaining) / static_cast<float>(juce::jmax(1, waveTableFadeSamples)));
            const auto targetLeft = linearInterpolate(targetWaveTableLeft[static_cast<size_t>(indexA)], targetWaveTableLeft[static_cast<size_t>(indexB)], frac);
            const auto targetRight = linearInterpolate(targetWaveTableRight[static_cast<size_t>(indexA)], targetWaveTableRight[static_cast<size_t>(indexB)], frac);
            leftSample = linearInterpolate(leftSample, targetLeft, fade);
            rightSample = linearInterpolate(rightSample, targetRight, fade);

            --waveTableFadeSamplesRemaining;
            if (waveTableFadeSamplesRemaining == 0)
            {
                currentWaveTableLeft = targetWaveTableLeft;
                currentWaveTableRight = targetWaveTableRight;

                if (hasPendingWaveTable)
                {
                    targetWaveTableLeft = pendingWaveTableLeft;
                    targetWaveTableRight = pendingWaveTableRight;
                    appliedWaveTableGeneration = pendingWaveTableGeneration;
                    if (pendingWaveTableSize != waveTableSize)
                    {
                        waveTableSize = pendingWaveTableSize;
                        currentWaveTableLeft = targetWaveTableLeft;
                        currentWaveTableRight = targetWaveTableRight;
                        waveTableFadeSamplesRemaining = 0;
                    }
                    else
                    {
                        waveTableFadeSamplesRemaining = waveTableFadeSamples;
                    }

                    hasPendingWaveTable = false;
                }
            }
        }

        const auto env = adsr.getNextSample() * envelopeOutputLevel;
        const auto leftValue = leftSample * env * level;
        const auto rightValue = rightSample * env * level;

        outputBuffer.addSample(0, startSample + sample, leftValue);

        if (outputBuffer.getNumChannels() > 1)
        {
            outputBuffer.addSample(1, startSample + sample, rightValue);
        }
        else
        {
            outputBuffer.addSample(0, startSample + sample, rightValue);
        }

        const auto pitchMultiplier = std::exp2(static_cast<double>(driftCurrentSemitone) / 12.0);
        phase += phaseDelta * pitchMultiplier;
        if (phase >= 1.0)
        {
            phase -= 1.0;
        }

        if (envelopeMode == EnvelopeMode::ar && !arReleaseTriggered)
        {
            --arReleaseSampleCountdown;
            if (arReleaseSampleCountdown <= 0)
            {
                adsr.noteOff();
                arReleaseTriggered = true;
            }
        }
    }

    if (!adsr.isActive())
    {
        retriggeringFromSteal = false;
        clearCurrentNote();
    }
}

void RoundRobinSynthesiser::setActiveVoiceLimit(int newLimit)
{
    activeVoiceLimit = juce::jlimit(0, getNumVoices(), newLimit);
    if (nextVoiceIndex >= activeVoiceLimit)
    {
        nextVoiceIndex = 0;
    }
}

juce::SynthesiserVoice* RoundRobinSynthesiser::findFreeVoice(juce::SynthesiserSound* soundToPlay,
                                                             int midiChannel,
                                                             int midiNoteNumber,
                                                             bool stealIfNoneAvailable) const
{
    juce::ignoreUnused(midiChannel, midiNoteNumber);

    const auto voiceCount = juce::jmin(activeVoiceLimit, getNumVoices());
    if (voiceCount <= 0)
    {
        return stealIfNoneAvailable ? findVoiceToSteal(soundToPlay, midiChannel, midiNoteNumber) : nullptr;
    }

    const auto startIndex = juce::jlimit(0, voiceCount - 1, nextVoiceIndex);
    for (int offset = 0; offset < voiceCount; ++offset)
    {
        const auto voiceIndex = (startIndex + offset) % voiceCount;
        if (auto* voice = getVoice(voiceIndex))
        {
            if (! voice->isVoiceActive() && voice->canPlaySound(soundToPlay))
            {
                nextVoiceIndex = (voiceIndex + 1) % voiceCount;
                return voice;
            }
        }
    }

    return stealIfNoneAvailable ? findVoiceToSteal(soundToPlay, midiChannel, midiNoteNumber) : nullptr;
}

juce::SynthesiserVoice* RoundRobinSynthesiser::findVoiceToSteal(juce::SynthesiserSound* soundToPlay,
                                                                int midiChannel,
                                                                int midiNoteNumber) const
{
    juce::ignoreUnused(midiChannel, midiNoteNumber);

    const auto voiceCount = juce::jmin(activeVoiceLimit, getNumVoices());
    if (voiceCount <= 0)
    {
        return nullptr;
    }

    juce::SynthesiserVoice* oldestVoice = nullptr;
    for (int i = 0; i < voiceCount; ++i)
    {
        auto* voice = getVoice(i);
        if (voice == nullptr || ! voice->canPlaySound(soundToPlay) || ! voice->isVoiceActive())
        {
            continue;
        }

        if (oldestVoice == nullptr || voice->wasStartedBefore(*oldestVoice))
        {
            oldestVoice = voice;
        }
    }

    return oldestVoice;
}

void SineWaveVoice::setAdsrSampleRate(double sampleRate)
{
    adsr.setSampleRate(sampleRate);
}

void SineWaveVoice::updateAdsr(float attackMs, float decayMs, float sustainLevel, float releaseMs, EnvelopeMode mode)
{
    envelopeMode = mode;
    const auto attackSeconds = attackMs / 1000.0f;
    const auto arMode = mode == EnvelopeMode::ar;
    const auto asrMode = mode == EnvelopeMode::asr;

    adsrParams.attack = attackSeconds;
    adsrParams.decay = (asrMode || arMode) ? 0.001f : decayMs / 1000.0f;
    adsrParams.sustain = (asrMode || arMode) ? 1.0f : sustainLevel;
    adsrParams.release = releaseMs / 1000.0f;
    envelopeOutputLevel = asrMode ? juce::jlimit(0.0f, 1.0f, sustainLevel) : 1.0f;
    arReleaseSampleCountdown = juce::jmax(1, static_cast<int>(std::round(currentSampleRate * attackSeconds)));
    adsr.setParameters(adsrParams);
}

void SineWaveVoice::setWaveTables(const float* leftTable, const float* rightTable, int size, uint32_t generation)
{
    if (leftTable == nullptr || rightTable == nullptr || size < 2)
    {
        return;
    }

    const auto clampedSize = juce::jlimit(2, kInternalWaveTableSize, size);
    if (hasWaveTable && generation == appliedWaveTableGeneration && waveTableSize == clampedSize)
    {
        return;
    }

    if (hasWaveTable && waveTableFadeSamplesRemaining > 0)
    {
        if (hasPendingWaveTable && generation == pendingWaveTableGeneration && pendingWaveTableSize == clampedSize)
        {
            return;
        }

        std::copy_n(leftTable, static_cast<size_t>(clampedSize), pendingWaveTableLeft.begin());
        std::copy_n(rightTable, static_cast<size_t>(clampedSize), pendingWaveTableRight.begin());
        pendingWaveTableSize = clampedSize;
        pendingWaveTableGeneration = generation;
        hasPendingWaveTable = true;
        return;
    }

    std::copy_n(leftTable, static_cast<size_t>(clampedSize), targetWaveTableLeft.begin());
    std::copy_n(rightTable, static_cast<size_t>(clampedSize), targetWaveTableRight.begin());

    if (!hasWaveTable || waveTableSize != clampedSize)
    {
        currentWaveTableLeft = targetWaveTableLeft;
        currentWaveTableRight = targetWaveTableRight;
        waveTableFadeSamplesRemaining = 0;
        hasWaveTable = true;
    }
    else
    {
        waveTableFadeSamplesRemaining = waveTableFadeSamples;
    }

    waveTableSize = clampedSize;
    appliedWaveTableGeneration = generation;
    hasPendingWaveTable = false;
}

void SineWaveVoice::setNoteDriftAmount(float amount)
{
    noteDriftAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void SineWaveVoice::setLiveNoteDriftRateHz(float rateHz)
{
    liveNoteDriftRateHz = juce::jmax(0.0f, rateHz);
}

void SineWaveVoice::setRandomSeed(uint32_t seed)
{
    randomGenerator.setSeedRandomly();
    randomGenerator.setSeed(static_cast<int64>(seed == 0u ? 1u : seed));
}

double SineWaveVoice::getPropellorPhaseOffset() const
{
    return static_cast<double>(propellorPhaseOffset) * juce::MathConstants<double>::twoPi;
}

uint32_t SineWaveVoice::getRandomModulationSeed() const
{
    return randomModulationSeed;
}

float SineWaveVoice::getNoteOnVelocity() const
{
    return noteOnVelocity;
}
    
    void SineWaveVoice::forceStop()
    {
        clearCurrentNote();
        adsr.reset();
    }

PictureWaveSynthAudioProcessor::PictureWaveSynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cacheParameterPointers();
    instanceRandom.setSeed(static_cast<int64>(makeUniqueSeed(this, 0x31415926u)));

    synth.clearVoices();
    voices.reserve(static_cast<size_t>(kMaxVoices));
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto* voice = new SineWaveVoice();
        voice->setRandomSeed(makeUniqueSeed(this, static_cast<uint32_t>(i + 1)));
        voices.push_back(voice);
        synth.addVoice(voice);
    }

    synth.clearSounds();
    synth.addSound(new SineWaveSound());

    perVoiceWaveTableLeft.resize(static_cast<size_t>(kMaxVoices));
    perVoiceWaveTableRight.resize(static_cast<size_t>(kMaxVoices));
    perVoiceWaveTableGenerations.resize(static_cast<size_t>(kMaxVoices), 0);
    perVoiceSmoothedModulationSums.resize(static_cast<size_t>(kMaxVoices));
    for (auto& sums : perVoiceSmoothedModulationSums)
    {
        sums.fill(0.0f);
    }
    perVoiceLastScannerParams.resize(static_cast<size_t>(kMaxVoices));
    perVoiceLastPropellorPhase.resize(static_cast<size_t>(kMaxVoices), 0.0);
    perVoiceHasCachedScannerState.resize(static_cast<size_t>(kMaxVoices), false);

    generateFallbackWaveTables(previewWaveTableLeft.data(), previewWaveTableRight.data());

    {
        setStateInformation(BinaryData::init_pspreset, BinaryData::init_pspresetSize);
    }

    getStateInformation(initialStateWithoutImage);
}

void PictureWaveSynthAudioProcessor::resetToInitialPreset()
{
    if (initialStateWithoutImage.getSize() == 0)
    {
        return;
    }

    setStateInformation(initialStateWithoutImage.getData(), static_cast<int>(initialStateWithoutImage.getSize()));
    waveTableDirty.store(true);
}

void PictureWaveSynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    waveTableDirty.store(true);
    propellorPhase.store(0.0);
    lfoPhases.fill(0.0);
    lfoCyclePositions.fill(0);
    for (auto& value : modulationDisplayValues)
    {
        value.store(0.0f);
    }
    for (auto& value : effectiveDisplayValues)
    {
        value.store(0.0f);
    }

    modulationVelocity = 0.0f;
    modulationAftertouch = 0.0f;
    modulationModWheel = 0.0f;
    heldNoteCount = 0;
    modulationPreviewSeed = static_cast<uint32_t>(instanceRandom.nextInt());
    waveTableGeneration = 0;
    smoothedModulationSums.fill(0.0f);
    smoothedPreviewModulationSums.fill(0.0f);
    std::fill(perVoiceWaveTableGenerations.begin(), perVoiceWaveTableGenerations.end(), 0u);
    for (auto& sums : perVoiceSmoothedModulationSums)
    {
        sums.fill(0.0f);
    }

    for (auto* voice : voices)
    {
        if (voice != nullptr)
        {
            voice->prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
            voice->setAdsrSampleRate(sampleRate);
        }
    }

    juce::dsp::ProcessSpec fxSpec;
    fxSpec.sampleRate = sampleRate;
    fxSpec.maximumBlockSize = static_cast<uint32_t>(juce::jmax(1, samplesPerBlock));
    fxSpec.numChannels = static_cast<uint32_t>(juce::jmax(1, getTotalNumOutputChannels()));
    dcBlocker.prepare(fxSpec);
    dcBlocker.reset();
    hasLastDcSampleRate = false;
    updateDcBlocker();
    fxFilter.prepare(fxSpec);
    fxFilter.reset();
    hasLastFxSettings = false;
    updateFxFilter();
    reverb.reset();
    hasLastReverbSettings = false;
    updateReverb();
}

void PictureWaveSynthAudioProcessor::releaseResources()
{
    dcBlocker.reset();
    fxFilter.reset();
    reverb.reset();

    for (auto& value : modulationDisplayValues)
    {
        value.store(0.0f);
    }
    for (auto& value : effectiveDisplayValues)
    {
        value.store(0.0f);
    }
    smoothedModulationSums.fill(0.0f);
    smoothedPreviewModulationSums.fill(0.0f);
    for (auto& sums : perVoiceSmoothedModulationSums)
    {
        sums.fill(0.0f);
    }
}

bool PictureWaveSynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PictureWaveSynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const auto loadValue = [this](std::atomic<float>* raw, const char* fallbackId)
    {
        if (raw != nullptr)
            return raw->load();
        if (auto* fallback = parameters.getRawParameterValue(fallbackId))
            return fallback->load();
        return 0.0f;
    };

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            modulationVelocity = juce::jlimit(0.0f, 1.0f, message.getFloatVelocity());
            ++heldNoteCount;
        }
        else if (message.isNoteOff())
        {
            heldNoteCount = juce::jmax(0, heldNoteCount - 1);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            heldNoteCount = 0;
        }
        else if (message.isChannelPressure())
        {
            modulationAftertouch = juce::jlimit(0.0f, 1.0f, static_cast<float>(message.getChannelPressureValue()) / 127.0f);
        }
        else if (message.isAftertouch())
        {
            modulationAftertouch = juce::jlimit(0.0f, 1.0f, static_cast<float>(message.getAfterTouchValue()) / 127.0f);
        }
        else if (message.isController() && message.getControllerNumber() == 1)
        {
            modulationModWheel = juce::jlimit(0.0f, 1.0f, static_cast<float>(message.getControllerValue()) / 127.0f);
        }
    }

    const std::array<int, 8> polyphonyChoices{ 4, 8, 12, 16, 24, 32, 48, 64 };
    const auto polyChoiceIndex = juce::jlimit(0, static_cast<int>(polyphonyChoices.size()) - 1,
                                              static_cast<int>(std::lround(loadValue(paramCache.maxVoices, "maxVoices"))));
    const auto maxVoices = polyphonyChoices[static_cast<size_t>(polyChoiceIndex)];
    synth.setActiveVoiceLimit(maxVoices);

    double hostBpm = 120.0;
    if (auto* playHead = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo positionInfo;
        if (playHead->getCurrentPosition(positionInfo) && positionInfo.bpm > 0.0)
        {
            hostBpm = positionInfo.bpm;
        }
    }

    std::array<float, kNumLfos> lfoValues{};
    std::array<float, kNumLfos> lfoDepths{};
    std::array<int, kNumLfos> lfoWaveforms{};
    std::array<bool, kNumLfos> lfoRandomPhasePerVoice{};
    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto rateHz = juce::jmax(0.01f, loadValue(paramCache.lfoRate[static_cast<size_t>(i)], "lfo1Rate"));
        const auto depth = juce::jlimit(0.0f, 1.0f, loadValue(paramCache.lfoDepth[static_cast<size_t>(i)], "lfo1Depth"));
        const auto waveform = static_cast<int>(std::lround(loadValue(paramCache.lfoWave[static_cast<size_t>(i)], "lfo1Wave")));
        const auto syncEnabled = loadValue(paramCache.lfoSync[static_cast<size_t>(i)], "lfo1Sync") > 0.5f;
        const auto division = static_cast<int>(std::lround(loadValue(paramCache.lfoDivision[static_cast<size_t>(i)], "lfo1Division")));
        const auto randomPhasePerVoice = loadValue(paramCache.lfoRandomPhasePerVoice[static_cast<size_t>(i)], "lfo1RandomPhasePerVoice") > 0.5f;

        lfoDepths[static_cast<size_t>(i)] = depth;
        lfoWaveforms[static_cast<size_t>(i)] = waveform;
        lfoRandomPhasePerVoice[static_cast<size_t>(i)] = randomPhasePerVoice;

        double effectiveRateHz = static_cast<double>(rateHz);
        if (syncEnabled)
        {
            const auto beatsPerSecond = hostBpm / 60.0;
            const auto beatsPerCycle = propellorDivisionToBeatsPerCycle(division);
            effectiveRateHz = beatsPerSecond / beatsPerCycle;
        }

        auto advancedPhase = lfoPhases[static_cast<size_t>(i)]
            + effectiveRateHz * static_cast<double>(buffer.getNumSamples()) / getSampleRate();
        const auto completedCycles = static_cast<int64_t>(std::floor(advancedPhase));
        if (completedCycles > 0)
        {
            lfoCyclePositions[static_cast<size_t>(i)] += completedCycles;
            advancedPhase -= static_cast<double>(completedCycles);
        }

        lfoPhases[static_cast<size_t>(i)] = advancedPhase;
        lfoValues[static_cast<size_t>(i)] = renderExtendedLfoSample(
            waveform,
            i,
            lfoCyclePositions[static_cast<size_t>(i)],
            lfoPhases[static_cast<size_t>(i)],
            0) * depth;
    }

    struct ModRouteState
    {
        bool enabled = false;
        int source = 0;
        int target = 0;
        float amount = 0.0f;
        bool bipolar = true;
        bool usesRandomWave = false;
        bool usesPerVoiceVariation = false;
    };

    std::array<ModRouteState, kNumModRoutes> routes{};
    std::array<float, kModTargetParamIds.size()> modulationSums{};
    modulationSums.fill(0.0f);
    const auto responseMs = juce::jlimit(0.0f, 500.0f, loadValue(paramCache.modResponseMs, "modResponseMs"));
    const auto responseSeconds = static_cast<double>(responseMs) * 0.001;
    const auto blockDurationSeconds = getSampleRate() > 0.0 ? static_cast<double>(buffer.getNumSamples()) / getSampleRate() : 0.0;
    const auto smoothingAlpha = (responseSeconds <= 0.0 || blockDurationSeconds <= 0.0)
        ? 1.0f
        : static_cast<float>(1.0 - std::exp(-blockDurationSeconds / responseSeconds));

    const auto applySmoothingToSums = [smoothingAlpha](const std::array<float, kNumModTargets>& input,
                                                       std::array<float, kNumModTargets>& state)
    {
        if (smoothingAlpha >= 1.0f)
        {
            state = input;
            return;
        }

        for (size_t i = 0; i < state.size(); ++i)
        {
            state[i] += (input[i] - state[i]) * smoothingAlpha;
        }
    };
    const auto sourceValueForIndex = [this, &lfoValues](int sourceIndex)
    {
        if (sourceIndex >= 0 && sourceIndex < kNumLfos)
        {
            return lfoValues[static_cast<size_t>(sourceIndex)];
        }

        switch (sourceIndex)
        {
            case StandardModSource::velocity:
                return modulationVelocity;
            case StandardModSource::aftertouch:
                return modulationAftertouch;
            case StandardModSource::modWheel:
                return modulationModWheel;
            case StandardModSource::noteGate:
                return heldNoteCount > 0 ? 1.0f : 0.0f;
            default:
                return 0.0f;
        }
    };

    const auto sourceValueForRoute = [](const ModRouteState& route, float rawSourceValue)
    {
        const auto sourceIsNaturallyBipolar = route.source >= 0 && route.source < kNumLfos;
        if (route.bipolar)
        {
            if (sourceIsNaturallyBipolar)
            {
                return rawSourceValue;
            }

            return juce::jlimit(-1.0f, 1.0f, rawSourceValue * 2.0f - 1.0f);
        }

        if (sourceIsNaturallyBipolar)
        {
            return juce::jlimit(0.0f, 1.0f, 0.5f * (rawSourceValue + 1.0f));
        }

        return juce::jlimit(0.0f, 1.0f, rawSourceValue);
    };

    for (int route = 1; route <= kNumModRoutes; ++route)
    {
        auto& routeState = routes[static_cast<size_t>(route - 1)];
        const auto routeIndex = static_cast<size_t>(route - 1);
        routeState.enabled = paramCache.modEnabled[routeIndex] != nullptr && paramCache.modEnabled[routeIndex]->load() > 0.5f;
        if (!routeState.enabled)
        {
            continue;
        }

        routeState.source = juce::jlimit(0, kNumModSources - 1, static_cast<int>(std::lround(paramCache.modSource[routeIndex]->load())));
        routeState.target = juce::jlimit(0, static_cast<int>(kModTargetParamIds.size()), static_cast<int>(std::lround(paramCache.modTarget[routeIndex]->load())));
        routeState.amount = juce::jlimit(-1.0f, 1.0f, paramCache.modAmount[routeIndex]->load());
        routeState.bipolar = paramCache.modBipolar[routeIndex]->load() > 0.5f;
        routeState.usesRandomWave = routeState.source < kNumLfos
            && isRandomLfoWaveform(lfoWaveforms[static_cast<size_t>(routeState.source)]);
        routeState.usesPerVoiceVariation = routeState.usesRandomWave
            || (routeState.source < kNumLfos
                && lfoRandomPhasePerVoice[static_cast<size_t>(routeState.source)]
                && !routeState.usesRandomWave)
            || routeState.source == StandardModSource::velocity;

        if (routeState.target <= 0)
        {
            continue;
        }

        const auto targetIndex = routeState.target - 1;
        if (targetIndex >= static_cast<int>(kModTargetParamIds.size()))
        {
            continue;
        }

        const auto sourceValue = sourceValueForRoute(routeState, sourceValueForIndex(routeState.source));
        modulationSums[static_cast<size_t>(targetIndex)] += routeState.amount * sourceValue;
    }

    applySmoothingToSums(modulationSums, smoothedModulationSums);

    for (size_t i = 0; i < smoothedModulationSums.size(); ++i)
    {
        modulationDisplayValues[i].store(smoothedModulationSums[i]);
    }

    const auto readParamByIndex = [this](int targetIndex, bool storeEffective)
    {
        auto* param = paramCache.modTargetParams[static_cast<size_t>(targetIndex)];
        auto* raw = paramCache.modTargetRaw[static_cast<size_t>(targetIndex)];
        if (param == nullptr || raw == nullptr)
        {
            return 0.0f;
        }

        const auto baseNorm = param->getValue();
        const auto modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + this->smoothedModulationSums[static_cast<size_t>(targetIndex)]);
        const auto actualValue = param->convertFrom0to1(modNorm);
        if (storeEffective)
        {
            effectiveDisplayValues[static_cast<size_t>(targetIndex)].store(actualValue);
        }

        return actualValue;
    };

    const auto attack = readParamByIndex(mtAttack, true);
    const auto decay = readParamByIndex(mtDecay, true);
    const auto sustain = readParamByIndex(mtSustain, true);
    const auto release = readParamByIndex(mtRelease, true);
    const auto envelopeMode = static_cast<EnvelopeMode>(juce::jlimit(0, 2, static_cast<int>(std::lround(loadValue(paramCache.envType, "envType")))));
    const auto gainDb = readParamByIndex(mtGain, true);
    const auto noteDriftAmount = readParamByIndex(mtNoteDrift, true);
    const auto liveNoteDriftHz = readParamByIndex(mtLiveNoteDrift, true);

    ScannerParams scanner;
    scanner.lineX1 = readParamByIndex(mtLineX1, true);
    scanner.lineY1 = readParamByIndex(mtLineY1, true);
    scanner.lineX2 = readParamByIndex(mtLineX2, true);
    scanner.lineY2 = readParamByIndex(mtLineY2, true);
    scanner.scanResolution = juce::jlimit(0, static_cast<int>(kScanResolutionValues.size()) - 1,
                                          static_cast<int>(std::lround(loadValue(paramCache.scanResolution, "scanResolution"))));
    scanner.useSplineInterpolation = loadValue(paramCache.scanSplineInterpolation, "scanSplineInterpolation") > 0.5f;
    scanner.mode = juce::jlimit(0, 4, static_cast<int>(std::lround(loadValue(paramCache.scannerMode, "scannerMode"))));
    scanner.ovalX1 = readParamByIndex(mtOvalX1, true);
    scanner.ovalY1 = readParamByIndex(mtOvalY1, true);
    scanner.ovalX2 = readParamByIndex(mtOvalX2, true);
    scanner.ovalY2 = readParamByIndex(mtOvalY2, true);
    scanner.ovalRotation = readParamByIndex(mtOvalRotation, true);
    scanner.rectX = readParamByIndex(mtRectX, true);
    scanner.rectY = readParamByIndex(mtRectY, true);
    scanner.rectWidth = readParamByIndex(mtRectWidth, true);
    scanner.rectHeight = readParamByIndex(mtRectHeight, true);
    scanner.rectRotation = readParamByIndex(mtRectRotation, true);
    scanner.triX1 = readParamByIndex(mtTriX1, true);
    scanner.triY1 = readParamByIndex(mtTriY1, true);
    scanner.triX2 = readParamByIndex(mtTriX2, true);
    scanner.triY2 = readParamByIndex(mtTriY2, true);
    scanner.triX3 = readParamByIndex(mtTriX3, true);
    scanner.triY3 = readParamByIndex(mtTriY3, true);
    scanner.propX = readParamByIndex(mtPropX, true);
    scanner.propY = readParamByIndex(mtPropY, true);
    scanner.propSize = readParamByIndex(mtPropSize, true);
    scanner.propSpeed = readParamByIndex(mtPropSpeed, true);
    scanner.propSyncDivision = juce::jlimit(0, static_cast<int>(kSyncDivisionNames.size()) - 1, static_cast<int>(std::lround(loadValue(paramCache.propSyncDivision, "propSyncDivision"))));
    scanner.propTempoSync = loadValue(paramCache.propTempoSync, "propTempoSync") > 0.5f;
    const auto randomPhaseEnabled = loadValue(paramCache.randomPhase, "randomPhase") > 0.5f;
    scanner.mapRL = readParamByIndex(mtMapRL, true);
    scanner.mapGL = readParamByIndex(mtMapGL, true);
    scanner.mapBL = readParamByIndex(mtMapBL, true);
    scanner.mapAL = readParamByIndex(mtMapAL, true);
    scanner.mapRR = readParamByIndex(mtMapRR, true);
    scanner.mapGR = readParamByIndex(mtMapGR, true);
    scanner.mapBR = readParamByIndex(mtMapBR, true);
    scanner.mapAR = readParamByIndex(mtMapAR, true);

    if (scanner.mode == 4)
    {
        waveTableDirty.store(true);
    }

    bool hasPerVoiceVariableRoutes = false;
    bool hasPerVoiceVariableScannerRoutes = false;
    for (const auto& route : routes)
    {
        if (!route.enabled || !route.usesPerVoiceVariation)
        {
            continue;
        }

        hasPerVoiceVariableRoutes = true;
        if (route.target >= 7)
        {
            hasPerVoiceVariableScannerRoutes = true;
        }
    }

    const auto usePerVoicePropellorPhase = scanner.mode == 4 && randomPhaseEnabled;
    const auto usePerVoiceScannerState = usePerVoicePropellorPhase || hasPerVoiceVariableScannerRoutes;

    std::shared_ptr<LoadedImageData> localImage;
    if (usePerVoiceScannerState)
    {
        const juce::SpinLock::ScopedLockType lock(imageLock);
        localImage = loadedImageData;
    }
    else
    {
        regenerateWaveTablesIfNeeded(scanner);
    }

    if (hasPerVoiceVariableRoutes)
    {
        std::array<float, kModTargetParamIds.size()> previewModulationSums{};
        previewModulationSums.fill(0.0f);
        for (const auto& route : routes)
        {
            if (!route.enabled)
            {
                continue;
            }

            auto sourceValue = sourceValueForIndex(route.source);
            if (route.source < kNumLfos && route.usesRandomWave)
            {
                sourceValue = renderExtendedLfoSample(
                    lfoWaveforms[static_cast<size_t>(route.source)],
                    route.source,
                    lfoCyclePositions[static_cast<size_t>(route.source)],
                    lfoPhases[static_cast<size_t>(route.source)],
                    modulationPreviewSeed) * lfoDepths[static_cast<size_t>(route.source)];
            }
            else if (route.source < kNumLfos && lfoRandomPhasePerVoice[static_cast<size_t>(route.source)])
            {
                const auto offsetPhase = lfoPhases[static_cast<size_t>(route.source)]
                    + phaseOffsetFromVoiceSeed(route.source, modulationPreviewSeed);
                sourceValue = renderLfoSample(lfoWaveforms[static_cast<size_t>(route.source)], offsetPhase)
                    * lfoDepths[static_cast<size_t>(route.source)];
            }

            sourceValue = sourceValueForRoute(route, sourceValue);

            if (route.target <= 0)
            {
                continue;
            }

            const auto targetIndex = route.target - 1;
            if (targetIndex >= static_cast<int>(kModTargetParamIds.size()))
            {
                continue;
            }

            previewModulationSums[static_cast<size_t>(targetIndex)] += route.amount * sourceValue;
        }

        applySmoothingToSums(previewModulationSums, smoothedPreviewModulationSums);

        for (size_t targetIndex = 0; targetIndex < kModTargetParamIds.size(); ++targetIndex)
        {
            modulationDisplayValues[targetIndex].store(smoothedPreviewModulationSums[targetIndex]);
            auto* param = paramCache.modTargetParams[targetIndex];
            if (param != nullptr)
            {
                const auto actualValue = param->convertFrom0to1(
                    juce::jlimit(0.0f, 1.0f, param->getValue() + smoothedPreviewModulationSums[targetIndex]));
                effectiveDisplayValues[targetIndex].store(actualValue);
            }
        }
    }

    const auto basePropellorPhase = propellorPhase.load();

    if (usePerVoiceScannerState)
    {
        auto previewScanner = scanner;
        const auto readPreviewParam = [this](int targetIndex, float fallbackValue)
        {
            if (targetIndex < 0 || targetIndex >= static_cast<int>(kNumModTargets))
            {
                return fallbackValue;
            }

            return effectiveDisplayValues[static_cast<size_t>(targetIndex)].load();
        };

        previewScanner.lineX1 = readPreviewParam(mtLineX1, previewScanner.lineX1);
        previewScanner.lineY1 = readPreviewParam(mtLineY1, previewScanner.lineY1);
        previewScanner.lineX2 = readPreviewParam(mtLineX2, previewScanner.lineX2);
        previewScanner.lineY2 = readPreviewParam(mtLineY2, previewScanner.lineY2);
        previewScanner.ovalX1 = readPreviewParam(mtOvalX1, previewScanner.ovalX1);
        previewScanner.ovalY1 = readPreviewParam(mtOvalY1, previewScanner.ovalY1);
        previewScanner.ovalX2 = readPreviewParam(mtOvalX2, previewScanner.ovalX2);
        previewScanner.ovalY2 = readPreviewParam(mtOvalY2, previewScanner.ovalY2);
        previewScanner.rectX = readPreviewParam(mtRectX, previewScanner.rectX);
        previewScanner.rectY = readPreviewParam(mtRectY, previewScanner.rectY);
        previewScanner.rectWidth = readPreviewParam(mtRectWidth, previewScanner.rectWidth);
        previewScanner.rectHeight = readPreviewParam(mtRectHeight, previewScanner.rectHeight);
        previewScanner.triX1 = readPreviewParam(mtTriX1, previewScanner.triX1);
        previewScanner.triY1 = readPreviewParam(mtTriY1, previewScanner.triY1);
        previewScanner.triX2 = readPreviewParam(mtTriX2, previewScanner.triX2);
        previewScanner.triY2 = readPreviewParam(mtTriY2, previewScanner.triY2);
        previewScanner.triX3 = readPreviewParam(mtTriX3, previewScanner.triX3);
        previewScanner.triY3 = readPreviewParam(mtTriY3, previewScanner.triY3);
        previewScanner.propX = readPreviewParam(mtPropX, previewScanner.propX);
        previewScanner.propY = readPreviewParam(mtPropY, previewScanner.propY);
        previewScanner.propSize = readPreviewParam(mtPropSize, previewScanner.propSize);
        previewScanner.propSpeed = readPreviewParam(mtPropSpeed, previewScanner.propSpeed);
        previewScanner.mapRL = readPreviewParam(mtMapRL, previewScanner.mapRL);
        previewScanner.mapGL = readPreviewParam(mtMapGL, previewScanner.mapGL);
        previewScanner.mapBL = readPreviewParam(mtMapBL, previewScanner.mapBL);
        previewScanner.mapAL = readPreviewParam(mtMapAL, previewScanner.mapAL);
        previewScanner.mapRR = readPreviewParam(mtMapRR, previewScanner.mapRR);
        previewScanner.mapGR = readPreviewParam(mtMapGR, previewScanner.mapGR);
        previewScanner.mapBR = readPreviewParam(mtMapBR, previewScanner.mapBR);
        previewScanner.mapAR = readPreviewParam(mtMapAR, previewScanner.mapAR);

        WaveTable previewLeft{};
        WaveTable previewRight{};
        if (localImage != nullptr && localImage->width > 1 && localImage->height > 1)
        {
            regenerateWaveTablesFromImage(*localImage,
                                          previewScanner,
                                          basePropellorPhase,
                                          previewLeft.data(),
                                          previewRight.data());
        }
        else
        {
            generateFallbackWaveTables(previewLeft.data(), previewRight.data());
        }

        updateWaveTablePreview(previewLeft.data(), previewRight.data());
    }

    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (auto* voice = voices[static_cast<size_t>(i)])
        {
            if (i >= maxVoices)
            {
                voice->forceStop();
                continue;
            }

            const auto voiceIsActive = voice->isVoiceActive();

            std::array<float, kModTargetParamIds.size()> voiceModulationSums = smoothedModulationSums;
            if (hasPerVoiceVariableRoutes && voiceIsActive)
            {
                voiceModulationSums.fill(0.0f);
                for (const auto& route : routes)
                {
                    if (!route.enabled)
                    {
                        continue;
                    }

                    auto sourceValue = sourceValueForIndex(route.source);
                    if (route.source == StandardModSource::velocity)
                    {
                        sourceValue = voice->getNoteOnVelocity();
                    }
                    if (route.source < kNumLfos && route.usesRandomWave)
                    {
                        sourceValue = renderExtendedLfoSample(
                            lfoWaveforms[static_cast<size_t>(route.source)],
                            route.source,
                            lfoCyclePositions[static_cast<size_t>(route.source)],
                            lfoPhases[static_cast<size_t>(route.source)],
                            voice->getRandomModulationSeed()) * lfoDepths[static_cast<size_t>(route.source)];
                    }
                    else if (route.source < kNumLfos && lfoRandomPhasePerVoice[static_cast<size_t>(route.source)])
                    {
                        const auto offsetPhase = lfoPhases[static_cast<size_t>(route.source)]
                            + phaseOffsetFromVoiceSeed(route.source, voice->getRandomModulationSeed());
                        sourceValue = renderLfoSample(lfoWaveforms[static_cast<size_t>(route.source)], offsetPhase)
                            * lfoDepths[static_cast<size_t>(route.source)];
                    }

                    sourceValue = sourceValueForRoute(route, sourceValue);

                    if (route.target <= 0)
                    {
                        continue;
                    }

                    const auto targetIndex = route.target - 1;
                    if (targetIndex >= static_cast<int>(kModTargetParamIds.size()))
                    {
                        continue;
                    }

                    voiceModulationSums[static_cast<size_t>(targetIndex)] += route.amount * sourceValue;
                }

                if (static_cast<size_t>(i) < perVoiceSmoothedModulationSums.size())
                {
                    applySmoothingToSums(voiceModulationSums, perVoiceSmoothedModulationSums[static_cast<size_t>(i)]);
                    voiceModulationSums = perVoiceSmoothedModulationSums[static_cast<size_t>(i)];
                }
            }

            const auto readVoiceParam = [this, &voiceModulationSums](int targetIndex)
            {
                auto* param = paramCache.modTargetParams[static_cast<size_t>(targetIndex)];
                auto* raw = paramCache.modTargetRaw[static_cast<size_t>(targetIndex)];
                if (param == nullptr || raw == nullptr)
                {
                    return 0.0f;
                }

                const auto baseNorm = param->getValue();
                const auto modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + voiceModulationSums[static_cast<size_t>(targetIndex)]);
                return param->convertFrom0to1(modNorm);
            };

            const auto useVoiceModulation = hasPerVoiceVariableRoutes && voiceIsActive;
            const auto voiceAttack = useVoiceModulation ? readVoiceParam(mtAttack) : attack;
            const auto voiceDecay = useVoiceModulation ? readVoiceParam(mtDecay) : decay;
            const auto voiceSustain = useVoiceModulation ? readVoiceParam(mtSustain) : sustain;
            const auto voiceNoteDrift = useVoiceModulation ? readVoiceParam(mtNoteDrift) : noteDriftAmount;
            const auto voiceLiveNoteDrift = useVoiceModulation ? readVoiceParam(mtLiveNoteDrift) : liveNoteDriftHz;

            voice->updateAdsr(voiceAttack,
                              voiceDecay,
                              voiceSustain,
                              useVoiceModulation ? readVoiceParam(mtRelease) : release,
                              envelopeMode);
            voice->setNoteDriftAmount(voiceNoteDrift);
            voice->setLiveNoteDriftRateHz(voiceLiveNoteDrift);
            voice->setRandomPropellorPhaseEnabled(usePerVoicePropellorPhase);

            if (usePerVoiceScannerState)
            {
                auto& left = perVoiceWaveTableLeft[static_cast<size_t>(i)];
                auto& right = perVoiceWaveTableRight[static_cast<size_t>(i)];

                if (!voiceIsActive)
                {
                    // Inactive voices keep their last cached table; avoid per-voice scanner rebuilds.
                    voice->setWaveTables(left.data(),
                                         right.data(),
                                         kWaveTableSize,
                                         perVoiceWaveTableGenerations[static_cast<size_t>(i)]);
                    continue;
                }

                auto voiceScanner = scanner;
                if (hasPerVoiceVariableScannerRoutes)
                {
                    voiceScanner.lineX1 = readVoiceParam(mtLineX1);
                    voiceScanner.lineY1 = readVoiceParam(mtLineY1);
                    voiceScanner.lineX2 = readVoiceParam(mtLineX2);
                    voiceScanner.lineY2 = readVoiceParam(mtLineY2);
                    voiceScanner.ovalX1 = readVoiceParam(mtOvalX1);
                    voiceScanner.ovalY1 = readVoiceParam(mtOvalY1);
                    voiceScanner.ovalX2 = readVoiceParam(mtOvalX2);
                    voiceScanner.ovalY2 = readVoiceParam(mtOvalY2);
                    voiceScanner.rectX = readVoiceParam(mtRectX);
                    voiceScanner.rectY = readVoiceParam(mtRectY);
                    voiceScanner.rectWidth = readVoiceParam(mtRectWidth);
                    voiceScanner.rectHeight = readVoiceParam(mtRectHeight);
                    voiceScanner.triX1 = readVoiceParam(mtTriX1);
                    voiceScanner.triY1 = readVoiceParam(mtTriY1);
                    voiceScanner.triX2 = readVoiceParam(mtTriX2);
                    voiceScanner.triY2 = readVoiceParam(mtTriY2);
                    voiceScanner.triX3 = readVoiceParam(mtTriX3);
                    voiceScanner.triY3 = readVoiceParam(mtTriY3);
                    voiceScanner.propX = readVoiceParam(mtPropX);
                    voiceScanner.propY = readVoiceParam(mtPropY);
                    voiceScanner.propSize = readVoiceParam(mtPropSize);
                    voiceScanner.propSpeed = readVoiceParam(mtPropSpeed);
                    voiceScanner.mapRL = readVoiceParam(mtMapRL);
                    voiceScanner.mapGL = readVoiceParam(mtMapGL);
                    voiceScanner.mapBL = readVoiceParam(mtMapBL);
                    voiceScanner.mapAL = readVoiceParam(mtMapAL);
                    voiceScanner.mapRR = readVoiceParam(mtMapRR);
                    voiceScanner.mapGR = readVoiceParam(mtMapGR);
                    voiceScanner.mapBR = readVoiceParam(mtMapBR);
                    voiceScanner.mapAR = readVoiceParam(mtMapAR);
                }

                const auto phaseValue = basePropellorPhase + voice->getPropellorPhaseOffset();
                const auto scannerChanged = !perVoiceHasCachedScannerState[static_cast<size_t>(i)]
                    || !scannerParamsEqual(voiceScanner, perVoiceLastScannerParams[static_cast<size_t>(i)]);
                const auto phaseChanged = std::abs(phaseValue - perVoiceLastPropellorPhase[static_cast<size_t>(i)]) > 1.0e-4;

                if ((scannerChanged || (voiceScanner.mode == 4 && phaseChanged))
                    && localImage != nullptr && localImage->width > 1 && localImage->height > 1)
                {
                    regenerateWaveTablesFromImage(*localImage,
                                                  voiceScanner,
                                                  phaseValue,
                                                  left.data(),
                                                  right.data());
                    perVoiceLastScannerParams[static_cast<size_t>(i)] = voiceScanner;
                    perVoiceLastPropellorPhase[static_cast<size_t>(i)] = phaseValue;
                    perVoiceHasCachedScannerState[static_cast<size_t>(i)] = true;
                    ++perVoiceWaveTableGenerations[static_cast<size_t>(i)];
                }
                else if (scannerChanged || !perVoiceHasCachedScannerState[static_cast<size_t>(i)])
                {
                    generateFallbackWaveTables(left.data(), right.data());
                    perVoiceLastScannerParams[static_cast<size_t>(i)] = voiceScanner;
                    perVoiceLastPropellorPhase[static_cast<size_t>(i)] = phaseValue;
                    perVoiceHasCachedScannerState[static_cast<size_t>(i)] = true;
                    ++perVoiceWaveTableGenerations[static_cast<size_t>(i)];
                }

                voice->setWaveTables(left.data(),
                                     right.data(),
                                     kWaveTableSize,
                                     perVoiceWaveTableGenerations[static_cast<size_t>(i)]);
            }
            else
            {
                voice->setWaveTables(waveTableLeft.data(),
                                     waveTableRight.data(),
                                     kWaveTableSize,
                                     waveTableGeneration);
            }
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    updateFxFilter();
    const auto fxSettings = getFxFilterSettings();
    if (fxSettings.type != fxFilterOff)
    {
        juce::dsp::AudioBlock<float> audioBlock(buffer);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        fxFilter.process(context);
    }

    updateReverb();
    if (buffer.getNumChannels() > 1)
    {
        reverb.processStereo(buffer.getWritePointer(0), buffer.getWritePointer(1), buffer.getNumSamples());
    }
    else if (buffer.getNumChannels() > 0)
    {
        reverb.processMono(buffer.getWritePointer(0), buffer.getNumSamples());
    }

    updateDcBlocker();
    {
        juce::dsp::AudioBlock<float> audioBlock(buffer);
        juce::dsp::ProcessContextReplacing<float> context(audioBlock);
        dcBlocker.process(context);
    }

    if (scanner.mode == 4 && getSampleRate() > 0.0)
    {
        auto propSpeedHz = static_cast<double>(juce::jmax(0.0f, scanner.propSpeed));
        if (scanner.propTempoSync)
        {
            double bpm = 120.0;
            if (auto* playHead = getPlayHead())
            {
                juce::AudioPlayHead::CurrentPositionInfo positionInfo;
                if (playHead->getCurrentPosition(positionInfo) && positionInfo.bpm > 0.0)
                {
                    bpm = positionInfo.bpm;
                }
            }

            const auto beatsPerSecond = bpm / 60.0;
            const auto beatsPerCycle = propellorDivisionToBeatsPerCycle(scanner.propSyncDivision);
            propSpeedHz = beatsPerSecond / beatsPerCycle;
        }

        const auto phaseIncrement = juce::MathConstants<double>::twoPi
            * propSpeedHz
            * static_cast<double>(buffer.getNumSamples())
            / getSampleRate();

        propellorPhase.store(std::fmod(propellorPhase.load() + phaseIncrement, juce::MathConstants<double>::twoPi));
    }

    const auto linearGain = juce::Decibels::decibelsToGain(gainDb);
    buffer.applyGain(linearGain);
}

void PictureWaveSynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    juce::MemoryBlock xmlData;
    copyXmlToBinary(*xml, xmlData);

    std::shared_ptr<LoadedImageData> localImage;
    {
        const juce::SpinLock::ScopedLockType lock(imageLock);
        localImage = loadedImageData;
    }

    juce::MemoryOutputStream stateStream(destData, false);
    stateStream.writeIntBigEndian(static_cast<int>(kStateMagic));
    stateStream.writeIntBigEndian(static_cast<int>(kStateVersion));
    stateStream.writeIntBigEndian(static_cast<int>(xmlData.getSize()));
    stateStream.write(xmlData.getData(), xmlData.getSize());

    if (localImage != nullptr && localImage->image.isValid())
    {
        juce::MemoryOutputStream imageStream;
        juce::PNGImageFormat pngFormat;

        if (pngFormat.writeImageToStream(localImage->image, imageStream))
        {
            stateStream.writeIntBigEndian(static_cast<int>(imageStream.getDataSize()));
            stateStream.write(imageStream.getData(), imageStream.getDataSize());
            return;
        }
    }

    stateStream.writeIntBigEndian(0);
}

void PictureWaveSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto restoreFromXml = [this](const juce::XmlElement& xmlState)
    {
        if (!xmlState.hasTagName(parameters.state.getType()))
        {
            return;
        }

        parameters.replaceState(juce::ValueTree::fromXml(xmlState));

        auto* embeddedImageXml = xmlState.getChildByName(kEmbeddedImageTag);
        if (embeddedImageXml == nullptr)
        {
            clearLoadedImage();
            return;
        }

        if (embeddedImageXml->getStringAttribute("encoding") != kEmbeddedImageEncoding)
        {
            clearLoadedImage();
            return;
        }

        const auto encodedImage = embeddedImageXml->getAllSubText().trim();
        if (encodedImage.isEmpty())
        {
            clearLoadedImage();
            return;
        }

        juce::MemoryOutputStream decoded;
        if (!juce::Base64::convertFromBase64(decoded, encodedImage))
        {
            clearLoadedImage();
            return;
        }

        const auto decodedImage = juce::ImageFileFormat::loadFrom(decoded.getData(), decoded.getDataSize());
        juce::String loadError;
        if (!applyLoadedImage(decodedImage, loadError))
        {
            clearLoadedImage();
        }
    };

    juce::MemoryInputStream input(data, static_cast<size_t>(sizeInBytes), false);
    if (sizeInBytes >= 12)
    {
        const auto magic = static_cast<uint32_t>(input.readIntBigEndian());
        const auto version = static_cast<uint32_t>(input.readIntBigEndian());
        if (magic == kStateMagic && version == kStateVersion)
        {
            const auto xmlSize = input.readIntBigEndian();
            if (xmlSize > 0 && xmlSize <= input.getNumBytesRemaining())
            {
                juce::MemoryBlock xmlData(static_cast<size_t>(xmlSize));
                input.read(xmlData.getData(), static_cast<size_t>(xmlSize));
                std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(xmlData.getData(), static_cast<int>(xmlData.getSize())));
                if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
                {
                    parameters.replaceState(juce::ValueTree::fromXml(*xmlState));

                    const auto imageSize = input.readIntBigEndian();
                    if (imageSize <= 0 || imageSize > input.getNumBytesRemaining())
                    {
                        clearLoadedImage();
                        return;
                    }

                    juce::MemoryBlock imageData(static_cast<size_t>(imageSize));
                    input.read(imageData.getData(), static_cast<size_t>(imageSize));
                    const auto decodedImage = juce::ImageFileFormat::loadFrom(imageData.getData(), imageData.getSize());
                    juce::String loadError;
                    if (!applyLoadedImage(decodedImage, loadError))
                    {
                        clearLoadedImage();
                    }

                    return;
                }
            }
        }
    }

    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
    {
        restoreFromXml(*xmlState);
    }
}

PictureWaveSynthAudioProcessor::ParameterLayout PictureWaveSynthAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack", juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f), 30.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "decay", "Decay", juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f), 150.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sustain", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release", juce::NormalisableRange<float>(1.0f, 5000.0f, 1.0f), 300.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain", juce::NormalisableRange<float>(-36.0f, 32.0f, 0.1f), -12.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "envType", "Envelope Type", juce::StringArray{ "ADSR", "ASR", "AR" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fxFilterType", "FX Filter Type", getFxFilterTypeNames(), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fxFilterCutoff", "FX Filter Cutoff", juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.25f), 1200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fxFilterResonance", "FX Filter Resonance", juce::NormalisableRange<float>(0.1f, 10.0f, 0.001f, 0.35f), 0.707f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "fxFilterGain", "FX Filter Gain", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbRoomSize", "Reverb Room Size", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbDamping", "Reverb Damping", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbWidth", "Reverb Width", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbWet", "Reverb Wet", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "reverbDry", "Reverb Dry", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "reverbFreeze", "Reverb Freeze", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noteDrift", "Note Drift", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "liveNoteDrift", "Drift Freq", juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "maxVoices", "Polyphony", juce::StringArray{ "4", "8", "12", "16", "24", "32", "48", "64" }, 3));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "modResponseMs", "Mod Response", juce::NormalisableRange<float>(0.0f, 500.0f, 0.1f), 25.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lineX1", "Line X1", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lineY1", "Line Y1", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lineX2", "Line X2", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lineY2", "Line Y2", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scanResolution", "Scan Resolution", juce::StringArray{ "32", "64", "128", "256", "512", "1024", "2048" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "scanSplineInterpolation", "Scan Spline Interpolation", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scannerMode", "Scanner Mode", juce::StringArray{ "Line", "Oval", "Rectangle", "Triangle", "Propellor" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ovalX1", "Oval X1", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ovalY1", "Oval Y1", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ovalX2", "Oval X2", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ovalY2", "Oval Y2", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ovalRotation", "Oval Rot", juce::NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectX", "Rect X", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectY", "Rect Y", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectWidth", "Rect Width", juce::NormalisableRange<float>(0.05f, 1.5f, 0.001f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectHeight", "Rect Height", juce::NormalisableRange<float>(0.05f, 1.5f, 0.001f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectRotation", "Rect Rot", juce::NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triX1", "Triangle X1", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triY1", "Triangle Y1", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triX2", "Triangle X2", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triY2", "Triangle Y2", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triX3", "Triangle X3", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "triY3", "Triangle Y3", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "propX", "Propellor X", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "propY", "Propellor Y", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "propSize", "Propellor Size", juce::NormalisableRange<float>(0.05f, 1.5f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "propSpeed", "Propellor Speed", juce::NormalisableRange<float>(0.0f, 10.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "propSyncDivision", "Prop Sync Division", juce::NormalisableRange<float>(0.0f, static_cast<float>(kSyncDivisionNames.size() - 1), 1.0f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "propTempoSync", "Prop Tempo Sync", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "randomPhase", "Random Propellor Phase", true));

    for (int i = 1; i <= kNumLfos; ++i)
    {
        const auto idx = juce::String(i);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "lfo" + idx + "Rate", "LFO " + idx + " Rate", juce::NormalisableRange<float>(0.01f, 20.0f, 0.001f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "lfo" + idx + "Depth", "LFO " + idx + " Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "lfo" + idx + "Wave", "LFO " + idx + " Wave", juce::StringArray{ "Sine", "Triangle", "Saw", "Square", "Random Steps", "Random Linear", "Random Perlin" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "lfo" + idx + "Sync", "LFO " + idx + " Sync", false));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "lfo" + idx + "RandomPhasePerVoice", "LFO " + idx + " Random Phase Per Voice", false));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "lfo" + idx + "Division", "LFO " + idx + " Division", juce::StringArray{ "1/1", "1/2", "1/4", "1/8", "1/16", "1/2T", "1/4T", "1/8T", "1/16T" }, 2));
    }

    juce::StringArray modSources;
    for (int i = 1; i <= kNumLfos; ++i)
    {
        modSources.add("LFO " + juce::String(i));
    }
    modSources.add("Velocity");
    modSources.add("Aftertouch");
    modSources.add("Mod Wheel");
    modSources.add("Note Gate");
    juce::StringArray modTargets;
    for (const auto* name : kModTargetNames)
    {
        modTargets.add(name);
    }

    for (int i = 1; i <= kNumModRoutes; ++i)
    {
        const auto idx = juce::String(i);
        const auto sourceDefault = static_cast<int>((i - 1) % kNumLfos);
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "mod" + idx + "Enabled", "Mod " + idx + " Enabled", false));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "mod" + idx + "Source", "Mod " + idx + " Source", modSources, sourceDefault));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "mod" + idx + "Target", "Mod " + idx + " Target", modTargets, 0));
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            "mod" + idx + "Bipolar", "Mod " + idx + " Bipolar", true));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "mod" + idx + "Amount", "Mod " + idx + " Amount", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapRL", "R -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapGL", "G -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapBL", "B -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), -0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapAL", "A -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapRR", "R -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), -0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapGR", "G -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapBR", "B -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapAR", "A -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));

    return { params.begin(), params.end() };
}

juce::AudioProcessorEditor* PictureWaveSynthAudioProcessor::createEditor()
{
    return new PictureWaveSynthAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PictureWaveSynthAudioProcessor();
}

bool PictureWaveSynthAudioProcessor::loadImageFromFile(const juce::File& imageFile, juce::String& errorMessage)
{
    if (!imageFile.existsAsFile())
    {
        errorMessage = "Selected file does not exist.";
        return false;
    }

    auto image = juce::ImageFileFormat::loadFrom(imageFile);
    return applyLoadedImage(image, errorMessage);
}

bool PictureWaveSynthAudioProcessor::applyLoadedImage(const juce::Image& image, juce::String& errorMessage)
{
    if (!image.isValid())
    {
        errorMessage = "Could not decode image data.";
        return false;
    }

    auto data = std::make_shared<LoadedImageData>();
    data->width = image.getWidth();
    data->height = image.getHeight();
    data->image = image;
    data->pixels.resize(static_cast<size_t>(data->width * data->height));

    for (int y = 0; y < data->height; ++y)
    {
        for (int x = 0; x < data->width; ++x)
        {
            data->pixels[static_cast<size_t>(y * data->width + x)] = image.getPixelAt(x, y);
        }
    }

    {
        const juce::SpinLock::ScopedLockType lock(imageLock);
        loadedImageData = std::move(data);
    }

    waveTableDirty.store(true);
    errorMessage.clear();
    return true;
}

void PictureWaveSynthAudioProcessor::clearLoadedImage()
{
    {
        const juce::SpinLock::ScopedLockType lock(imageLock);
        loadedImageData.reset();
    }

    waveTableDirty.store(true);
}

juce::Image PictureWaveSynthAudioProcessor::getLoadedImageCopy() const
{
    const juce::SpinLock::ScopedLockType lock(imageLock);
    if (loadedImageData == nullptr)
    {
        return {};
    }

    return loadedImageData->image;
}

double PictureWaveSynthAudioProcessor::getPropellorPhase() const
{
    return propellorPhase.load();
}

float PictureWaveSynthAudioProcessor::getModulationAmountForParameter(const char* paramId) const
{
    const auto index = modTargetIndexForParamId(paramId);
    if (index < 0)
    {
        return 0.0f;
    }

    return modulationDisplayValues[static_cast<size_t>(index)].load();
}

float PictureWaveSynthAudioProcessor::getEffectiveParameterValue(const char* paramId) const
{
    const auto index = modTargetIndexForParamId(paramId);
    if (index < 0)
    {
        if (auto* raw = parameters.getRawParameterValue(paramId))
        {
            return raw->load();
        }

        return 0.0f;
    }

    return effectiveDisplayValues[static_cast<size_t>(index)].load();
}

PictureWaveSynthAudioProcessor::FxFilterSettings PictureWaveSynthAudioProcessor::getFxFilterSettings() const
{
    FxFilterSettings settings;
    settings.type = static_cast<int>(std::lround(paramCache.fxFilterType != nullptr ? paramCache.fxFilterType->load() : 0.0f));
    settings.cutoffHz = paramCache.fxFilterCutoff != nullptr ? paramCache.fxFilterCutoff->load() : 1200.0f;
    settings.resonance = paramCache.fxFilterResonance != nullptr ? paramCache.fxFilterResonance->load() : 0.707f;
    settings.gainDecibels = paramCache.fxFilterGain != nullptr ? paramCache.fxFilterGain->load() : 0.0f;
    return settings;
}

PictureWaveSynthAudioProcessor::ReverbSettings PictureWaveSynthAudioProcessor::getReverbSettings() const
{
    ReverbSettings settings;
    settings.roomSize = paramCache.reverbRoomSize != nullptr ? paramCache.reverbRoomSize->load() : 0.35f;
    settings.damping = paramCache.reverbDamping != nullptr ? paramCache.reverbDamping->load() : 0.5f;
    settings.width = paramCache.reverbWidth != nullptr ? paramCache.reverbWidth->load() : 1.0f;
    settings.wetLevel = paramCache.reverbWet != nullptr ? paramCache.reverbWet->load() : 0.0f;
    settings.dryLevel = paramCache.reverbDry != nullptr ? paramCache.reverbDry->load() : 1.0f;
    settings.freezeMode = paramCache.reverbFreeze != nullptr ? paramCache.reverbFreeze->load() : 0.0f;
    return settings;
}

juce::StringArray PictureWaveSynthAudioProcessor::getFxFilterTypeNames()
{
    return {
        "Off",
        "Low-pass",
        "High-pass",
        "Band-pass",
        "Notch",
        "Peak",
        "Low Shelf",
        "High Shelf",
        "All-pass"
    };
}

bool PictureWaveSynthAudioProcessor::fxFilterTypeUsesGain(int type)
{
    return type == fxFilterPeak || type == fxFilterLowShelf || type == fxFilterHighShelf;
}

PictureWaveSynthAudioProcessor::IIRCoefficientsPtr PictureWaveSynthAudioProcessor::createDcBlockerCoefficients(double sampleRate)
{
    const auto safeSampleRate = juce::jmax(1.0, sampleRate);
    return juce::dsp::IIR::Coefficients<float>::makeHighPass(safeSampleRate, 15.0, 0.70710678118);
}

PictureWaveSynthAudioProcessor::IIRCoefficientsPtr PictureWaveSynthAudioProcessor::createFxFilterCoefficients(const FxFilterSettings& settings,
                                                                                                              double sampleRate)
{
    const auto safeSampleRate = juce::jmax(1.0, sampleRate);
    const auto cutoff = static_cast<double>(juce::jlimit(20.0f, 20000.0f, settings.cutoffHz));
    const auto q = static_cast<double>(juce::jlimit(0.1f, 10.0f, settings.resonance));
    const auto gain = static_cast<double>(juce::Decibels::decibelsToGain(settings.gainDecibels));

    switch (settings.type)
    {
        case fxFilterLowPass: return juce::dsp::IIR::Coefficients<float>::makeLowPass(safeSampleRate, cutoff, q);
        case fxFilterHighPass: return juce::dsp::IIR::Coefficients<float>::makeHighPass(safeSampleRate, cutoff, q);
        case fxFilterBandPass: return juce::dsp::IIR::Coefficients<float>::makeBandPass(safeSampleRate, cutoff, q);
        case fxFilterNotch: return juce::dsp::IIR::Coefficients<float>::makeNotch(safeSampleRate, cutoff, q);
        case fxFilterPeak: return juce::dsp::IIR::Coefficients<float>::makePeakFilter(safeSampleRate, cutoff, q, gain);
        case fxFilterLowShelf: return juce::dsp::IIR::Coefficients<float>::makeLowShelf(safeSampleRate, cutoff, q, gain);
        case fxFilterHighShelf: return juce::dsp::IIR::Coefficients<float>::makeHighShelf(safeSampleRate, cutoff, q, gain);
        case fxFilterAllPass: return juce::dsp::IIR::Coefficients<float>::makeAllPass(safeSampleRate, cutoff, q);
        case fxFilterOff:
        default:
            return {};
    }
}

void PictureWaveSynthAudioProcessor::copyCurrentWaveTablePreview(WaveTable& left, WaveTable& right) const
{
    const juce::SpinLock::ScopedLockType lock(waveTablePreviewLock);
    left = previewWaveTableLeft;
    right = previewWaveTableRight;
}

bool PictureWaveSynthAudioProcessor::hasLoadedImage() const
{
    const juce::SpinLock::ScopedLockType lock(imageLock);
    return loadedImageData != nullptr;
}

void PictureWaveSynthAudioProcessor::updateWaveTablePreview(const float* left, const float* right)
{
    const juce::SpinLock::ScopedLockType lock(waveTablePreviewLock);
    std::copy(left, left + kWaveTableSize, previewWaveTableLeft.begin());
    std::copy(right, right + kWaveTableSize, previewWaveTableRight.begin());
}

void PictureWaveSynthAudioProcessor::cacheParameterPointers()
{
    const auto cacheRaw = [this](const juce::String& id) -> std::atomic<float>*
    {
        return parameters.getRawParameterValue(id);
    };

    paramCache.maxVoices = cacheRaw("maxVoices");
    paramCache.modResponseMs = cacheRaw("modResponseMs");
    paramCache.envType = cacheRaw("envType");
    paramCache.scanResolution = cacheRaw("scanResolution");
    paramCache.scanSplineInterpolation = cacheRaw("scanSplineInterpolation");
    paramCache.scannerMode = cacheRaw("scannerMode");
    paramCache.propSyncDivision = cacheRaw("propSyncDivision");
    paramCache.propTempoSync = cacheRaw("propTempoSync");
    paramCache.randomPhase = cacheRaw("randomPhase");

    paramCache.fxFilterType = cacheRaw("fxFilterType");
    paramCache.fxFilterCutoff = cacheRaw("fxFilterCutoff");
    paramCache.fxFilterResonance = cacheRaw("fxFilterResonance");
    paramCache.fxFilterGain = cacheRaw("fxFilterGain");

    paramCache.reverbRoomSize = cacheRaw("reverbRoomSize");
    paramCache.reverbDamping = cacheRaw("reverbDamping");
    paramCache.reverbWidth = cacheRaw("reverbWidth");
    paramCache.reverbWet = cacheRaw("reverbWet");
    paramCache.reverbDry = cacheRaw("reverbDry");
    paramCache.reverbFreeze = cacheRaw("reverbFreeze");

    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto idx = juce::String(i + 1);
        paramCache.lfoRate[static_cast<size_t>(i)] = cacheRaw("lfo" + idx + "Rate");
        paramCache.lfoDepth[static_cast<size_t>(i)] = cacheRaw("lfo" + idx + "Depth");
        paramCache.lfoWave[static_cast<size_t>(i)] = cacheRaw("lfo" + idx + "Wave");
        paramCache.lfoSync[static_cast<size_t>(i)] = cacheRaw("lfo" + idx + "Sync");
        paramCache.lfoDivision[static_cast<size_t>(i)] = cacheRaw("lfo" + idx + "Division");
        paramCache.lfoRandomPhasePerVoice[static_cast<size_t>(i)] = cacheRaw("lfo" + idx + "RandomPhasePerVoice");
    }

    for (int i = 0; i < kNumModRoutes; ++i)
    {
        const auto idx = juce::String(i + 1);
        paramCache.modEnabled[static_cast<size_t>(i)] = cacheRaw("mod" + idx + "Enabled");
        paramCache.modSource[static_cast<size_t>(i)] = cacheRaw("mod" + idx + "Source");
        paramCache.modTarget[static_cast<size_t>(i)] = cacheRaw("mod" + idx + "Target");
        paramCache.modAmount[static_cast<size_t>(i)] = cacheRaw("mod" + idx + "Amount");
        paramCache.modBipolar[static_cast<size_t>(i)] = cacheRaw("mod" + idx + "Bipolar");
    }

    for (size_t i = 0; i < kModTargetParamIds.size(); ++i)
    {
        paramCache.modTargetParams[i] = parameters.getParameter(kModTargetParamIds[i]);
        paramCache.modTargetRaw[i] = parameters.getRawParameterValue(kModTargetParamIds[i]);
    }
}

void PictureWaveSynthAudioProcessor::updateDcBlocker()
{
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    if (hasLastDcSampleRate && juce::approximatelyEqual(sampleRate, lastDcSampleRate))
    {
        return;
    }

    const auto coefficients = createDcBlockerCoefficients(sampleRate);
    if (coefficients != nullptr)
    {
        *dcBlocker.state = *coefficients;
        lastDcSampleRate = sampleRate;
        hasLastDcSampleRate = true;
    }
}

void PictureWaveSynthAudioProcessor::updateFxFilter()
{
    const auto sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const auto settings = getFxFilterSettings();
    if (hasLastFxSettings
        && juce::approximatelyEqual(sampleRate, lastFxSampleRate)
        && fxSettingsEqual(settings, lastFxSettings))
    {
        return;
    }

    const auto coefficients = createFxFilterCoefficients(settings, sampleRate);
    if (coefficients != nullptr)
    {
        *fxFilter.state = *coefficients;
        lastFxSettings = settings;
        lastFxSampleRate = sampleRate;
        hasLastFxSettings = true;
    }
}

void PictureWaveSynthAudioProcessor::updateReverb()
{
    const auto settings = getReverbSettings();
    if (hasLastReverbSettings && reverbSettingsEqual(settings, lastReverbSettings))
    {
        return;
    }

    juce::Reverb::Parameters params;
    params.roomSize = settings.roomSize;
    params.damping = settings.damping;
    params.width = settings.width;
    params.wetLevel = settings.wetLevel;
    params.dryLevel = settings.dryLevel;
    params.freezeMode = settings.freezeMode;
    reverb.setParameters(params);
    lastReverbSettings = settings;
    hasLastReverbSettings = true;
}

void PictureWaveSynthAudioProcessor::regenerateWaveTablesIfNeeded(const ScannerParams& scanner)
{
    const auto scannerChanged = !hasLastScannerParams || !scannerParamsEqual(scanner, lastScannerParams);
    if (!scannerChanged && !waveTableDirty.load())
    {
        return;
    }

    std::shared_ptr<LoadedImageData> localImage;
    {
        const juce::SpinLock::ScopedLockType lock(imageLock);
        localImage = loadedImageData;
    }

    if (localImage != nullptr && localImage->width > 1 && localImage->height > 1)
    {
        regenerateWaveTablesFromImage(*localImage,
                                      scanner,
                                      propellorPhase.load(),
                                      waveTableLeft.data(),
                                      waveTableRight.data());
    }
    else
    {
        generateFallbackWaveTables(waveTableLeft.data(), waveTableRight.data());
    }

    updateWaveTablePreview(waveTableLeft.data(), waveTableRight.data());

    lastScannerParams = scanner;
    hasLastScannerParams = true;
    ++waveTableGeneration;
    waveTableDirty.store(false);
}

void PictureWaveSynthAudioProcessor::regenerateWaveTablesFromImage(const LoadedImageData& imageData,
                                                                   const ScannerParams& scanner,
                                                                   double propellorPhaseValue,
                                                                   float* outLeft,
                                                                   float* outRight)
{
    const auto sampleCount = kScanResolutionValues[static_cast<size_t>(juce::jlimit(0, static_cast<int>(kScanResolutionValues.size()) - 1,
                                                                                     scanner.scanResolution))];
    std::array<float, kWaveTableSize> sampledLeft{};
    std::array<float, kWaveTableSize> sampledRight{};
    float sumLeft = 0.0f;
    float sumRight = 0.0f;

    for (int i = 0; i < sampleCount; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(juce::jmax(1, sampleCount - 1));
        const auto uv = sampleScannerPoint(scanner, t, propellorPhaseValue);
        const auto u = juce::jlimit(0.0f, 1.0f, uv[0]);
        const auto v = juce::jlimit(0.0f, 1.0f, uv[1]);

        const auto rgba = sampleImageBilinear(imageData, u, v);

        const auto r = rgba[0] * 2.0f - 1.0f;
        const auto g = rgba[1] * 2.0f - 1.0f;
        const auto b = rgba[2] * 2.0f - 1.0f;
        const auto a = rgba[3] * 2.0f - 1.0f;

        const auto left = juce::jlimit(-1.0f, 1.0f,
            r * scanner.mapRL + g * scanner.mapGL + b * scanner.mapBL + a * scanner.mapAL);
        const auto right = juce::jlimit(-1.0f, 1.0f,
            r * scanner.mapRR + g * scanner.mapGR + b * scanner.mapBR + a * scanner.mapAR);

        sampledLeft[static_cast<size_t>(i)] = left;
        sampledRight[static_cast<size_t>(i)] = right;

        sumLeft += left;
        sumRight += right;
    }

    for (int i = 0; i < kWaveTableSize; ++i)
    {
        const auto sourcePos = static_cast<float>(i) * static_cast<float>(sampleCount - 1)
            / static_cast<float>(juce::jmax(1, kWaveTableSize - 1));
        const auto indexA = juce::jlimit(0, sampleCount - 1, static_cast<int>(std::floor(sourcePos)));
        const auto frac = sourcePos - static_cast<float>(indexA);

        if (scanner.useSplineInterpolation)
        {
            const auto index0 = juce::jlimit(0, sampleCount - 1, indexA - 1);
            const auto index1 = indexA;
            const auto index2 = juce::jlimit(0, sampleCount - 1, indexA + 1);
            const auto index3 = juce::jlimit(0, sampleCount - 1, indexA + 2);

            outLeft[static_cast<size_t>(i)] = catmullRomInterpolate(
                sampledLeft[static_cast<size_t>(index0)],
                sampledLeft[static_cast<size_t>(index1)],
                sampledLeft[static_cast<size_t>(index2)],
                sampledLeft[static_cast<size_t>(index3)],
                frac);
            outRight[static_cast<size_t>(i)] = catmullRomInterpolate(
                sampledRight[static_cast<size_t>(index0)],
                sampledRight[static_cast<size_t>(index1)],
                sampledRight[static_cast<size_t>(index2)],
                sampledRight[static_cast<size_t>(index3)],
                frac);
        }
        else
        {
            const auto indexB = juce::jlimit(0, sampleCount - 1, indexA + 1);
            outLeft[static_cast<size_t>(i)] = linearInterpolate(sampledLeft[static_cast<size_t>(indexA)], sampledLeft[static_cast<size_t>(indexB)], frac);
            outRight[static_cast<size_t>(i)] = linearInterpolate(sampledRight[static_cast<size_t>(indexA)], sampledRight[static_cast<size_t>(indexB)], frac);
        }

    }
    const auto meanLeft = sumLeft / static_cast<float>(kWaveTableSize);
    const auto meanRight = sumRight / static_cast<float>(kWaveTableSize);

    float maxAbsLeft = 0.0001f;
    float maxAbsRight = 0.0001f;
    for (int i = 0; i < kWaveTableSize; ++i)
    {
        outLeft[static_cast<size_t>(i)] -= meanLeft;
        outRight[static_cast<size_t>(i)] -= meanRight;

        maxAbsLeft = juce::jmax(maxAbsLeft, std::abs(outLeft[static_cast<size_t>(i)]));
        maxAbsRight = juce::jmax(maxAbsRight, std::abs(outRight[static_cast<size_t>(i)]));
    }

    const auto normaliseLeft = 0.9f / maxAbsLeft;
    const auto normaliseRight = 0.9f / maxAbsRight;
    for (int i = 0; i < kWaveTableSize; ++i)
    {
        outLeft[static_cast<size_t>(i)] *= normaliseLeft;
        outRight[static_cast<size_t>(i)] *= normaliseRight;
    }
}

void PictureWaveSynthAudioProcessor::generateFallbackWaveTables(float* outLeft, float* outRight)
{
    for (int i = 0; i < kWaveTableSize; ++i)
    {
        const auto phase = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(kWaveTableSize);
        const auto sample = std::sin(phase) * 0.9f;
        outLeft[static_cast<size_t>(i)] = sample;
        outRight[static_cast<size_t>(i)] = sample;
    }
}

std::array<float, 2> PictureWaveSynthAudioProcessor::sampleScannerPoint(const ScannerParams& scanner, float t, double propellorPhaseValue)
{
    const auto tt = juce::jlimit(0.0f, 1.0f, t);

    if (scanner.mode == 1)
    {
        const auto minX = juce::jmin(scanner.ovalX1, scanner.ovalX2);
        const auto maxX = juce::jmax(scanner.ovalX1, scanner.ovalX2);
        const auto minY = juce::jmin(scanner.ovalY1, scanner.ovalY2);
        const auto maxY = juce::jmax(scanner.ovalY1, scanner.ovalY2);

        const auto cx = 0.5f * (minX + maxX);
        const auto cy = 0.5f * (minY + maxY);
        const auto rx = juce::jmax(0.0001f, 0.5f * (maxX - minX));
        const auto ry = juce::jmax(0.0001f, 0.5f * (maxY - minY));
        const auto theta = juce::MathConstants<float>::twoPi * tt;
        const auto rotationRadians = juce::degreesToRadians(scanner.ovalRotation);

        return rotatePointAroundCenter(
            cx + rx * std::cos(theta),
            cy + ry * std::sin(theta),
            cx,
            cy,
            rotationRadians);
    }

    if (scanner.mode == 2)
    {
        const auto cx = scanner.rectX;
        const auto cy = scanner.rectY;
        const auto halfW = 0.5f * juce::jmax(0.001f, scanner.rectWidth);
        const auto halfH = 0.5f * juce::jmax(0.001f, scanner.rectHeight);

        const auto x0 = cx - halfW;
        const auto x1 = cx + halfW;
        const auto y0 = cy - halfH;
        const auto y1 = cy + halfH;

        std::array<float, 2> point{};
        const auto edgeT = tt * 4.0f;
        if (edgeT < 1.0f)
        {
            point = { linearInterpolate(x0, x1, edgeT), y0 };
        }
        else if (edgeT < 2.0f)
        {
            point = { x1, linearInterpolate(y0, y1, edgeT - 1.0f) };
        }
        else if (edgeT < 3.0f)
        {
            point = { linearInterpolate(x1, x0, edgeT - 2.0f), y1 };
        }
        else
        {
            point = { x0, linearInterpolate(y1, y0, edgeT - 3.0f) };
        }

        return rotatePointAroundCenter(
            point[0],
            point[1],
            cx,
            cy,
            juce::degreesToRadians(scanner.rectRotation));
    }

    if (scanner.mode == 3)
    {
        const std::array<float, 2> p1{ scanner.triX1, scanner.triY1 };
        const std::array<float, 2> p2{ scanner.triX2, scanner.triY2 };
        const std::array<float, 2> p3{ scanner.triX3, scanner.triY3 };

        const auto seg = tt * 3.0f;
        if (seg < 1.0f)
        {
            return { linearInterpolate(p1[0], p2[0], seg), linearInterpolate(p1[1], p2[1], seg) };
        }
        if (seg < 2.0f)
        {
            return { linearInterpolate(p2[0], p3[0], seg - 1.0f), linearInterpolate(p2[1], p3[1], seg - 1.0f) };
        }

        return { linearInterpolate(p3[0], p1[0], seg - 2.0f), linearInterpolate(p3[1], p1[1], seg - 2.0f) };
    }

    if (scanner.mode == 4)
    {
        const auto angle = static_cast<float>(propellorPhaseValue);
        const auto dx = std::cos(angle) * scanner.propSize;
        const auto dy = std::sin(angle) * scanner.propSize;
        const auto startX = scanner.propX - 0.5f * dx;
        const auto startY = scanner.propY - 0.5f * dy;
        return { startX + dx * tt, startY + dy * tt };
    }

    return {
        linearInterpolate(scanner.lineX1, scanner.lineX2, tt),
        linearInterpolate(scanner.lineY1, scanner.lineY2, tt)
    };
}

std::array<float, 4> PictureWaveSynthAudioProcessor::sampleImageBilinear(const LoadedImageData& imageData, float u, float v)
{
    const auto x = u * static_cast<float>(imageData.width - 1);
    const auto y = v * static_cast<float>(imageData.height - 1);

    const auto x0 = static_cast<int>(std::floor(x));
    const auto y0 = static_cast<int>(std::floor(y));
    const auto x1 = juce::jmin(imageData.width - 1, x0 + 1);
    const auto y1 = juce::jmin(imageData.height - 1, y0 + 1);

    const auto fx = x - static_cast<float>(x0);
    const auto fy = y - static_cast<float>(y0);

    const auto pixelAt = [&imageData](int px, int py)
    {
        return imageData.pixels[static_cast<size_t>(py * imageData.width + px)];
    };

    const auto c00 = pixelAt(x0, y0);
    const auto c10 = pixelAt(x1, y0);
    const auto c01 = pixelAt(x0, y1);
    const auto c11 = pixelAt(x1, y1);

    const auto blend = [fx, fy](float a00, float a10, float a01, float a11)
    {
        const auto top = linearInterpolate(a00, a10, fx);
        const auto bottom = linearInterpolate(a01, a11, fx);
        return linearInterpolate(top, bottom, fy);
    };

    return {
        blend(static_cast<float>(c00.getFloatRed()), static_cast<float>(c10.getFloatRed()), static_cast<float>(c01.getFloatRed()), static_cast<float>(c11.getFloatRed())),
        blend(static_cast<float>(c00.getFloatGreen()), static_cast<float>(c10.getFloatGreen()), static_cast<float>(c01.getFloatGreen()), static_cast<float>(c11.getFloatGreen())),
        blend(static_cast<float>(c00.getFloatBlue()), static_cast<float>(c10.getFloatBlue()), static_cast<float>(c01.getFloatBlue()), static_cast<float>(c11.getFloatBlue())),
        blend(static_cast<float>(c00.getFloatAlpha()), static_cast<float>(c10.getFloatAlpha()), static_cast<float>(c01.getFloatAlpha()), static_cast<float>(c11.getFloatAlpha()))
    };
}

bool PictureWaveSynthAudioProcessor::scannerParamsEqual(const ScannerParams& a, const ScannerParams& b)
{
    return a.mode == b.mode
        && juce::approximatelyEqual(a.lineX1, b.lineX1)
        && juce::approximatelyEqual(a.lineY1, b.lineY1)
        && juce::approximatelyEqual(a.lineX2, b.lineX2)
        && juce::approximatelyEqual(a.lineY2, b.lineY2)
        && a.scanResolution == b.scanResolution
        && a.useSplineInterpolation == b.useSplineInterpolation
        && juce::approximatelyEqual(a.ovalX1, b.ovalX1)
        && juce::approximatelyEqual(a.ovalY1, b.ovalY1)
        && juce::approximatelyEqual(a.ovalX2, b.ovalX2)
        && juce::approximatelyEqual(a.ovalY2, b.ovalY2)
        && juce::approximatelyEqual(a.ovalRotation, b.ovalRotation)
        && juce::approximatelyEqual(a.rectX, b.rectX)
        && juce::approximatelyEqual(a.rectY, b.rectY)
        && juce::approximatelyEqual(a.rectWidth, b.rectWidth)
        && juce::approximatelyEqual(a.rectHeight, b.rectHeight)
        && juce::approximatelyEqual(a.rectRotation, b.rectRotation)
        && juce::approximatelyEqual(a.triX1, b.triX1)
        && juce::approximatelyEqual(a.triY1, b.triY1)
        && juce::approximatelyEqual(a.triX2, b.triX2)
        && juce::approximatelyEqual(a.triY2, b.triY2)
        && juce::approximatelyEqual(a.triX3, b.triX3)
        && juce::approximatelyEqual(a.triY3, b.triY3)
        && juce::approximatelyEqual(a.propX, b.propX)
        && juce::approximatelyEqual(a.propY, b.propY)
        && juce::approximatelyEqual(a.propSize, b.propSize)
        && juce::approximatelyEqual(a.propSpeed, b.propSpeed)
        && a.propSyncDivision == b.propSyncDivision
        && a.propTempoSync == b.propTempoSync
        && juce::approximatelyEqual(a.mapRL, b.mapRL)
        && juce::approximatelyEqual(a.mapGL, b.mapGL)
        && juce::approximatelyEqual(a.mapBL, b.mapBL)
        && juce::approximatelyEqual(a.mapAL, b.mapAL)
        && juce::approximatelyEqual(a.mapRR, b.mapRR)
        && juce::approximatelyEqual(a.mapGR, b.mapGR)
        && juce::approximatelyEqual(a.mapBR, b.mapBR)
        && juce::approximatelyEqual(a.mapAR, b.mapAR);
}
