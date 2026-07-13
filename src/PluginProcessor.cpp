#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
constexpr int kMaxVoices = 32;
constexpr const char* kEmbeddedImageTag = "EMBEDDED_IMAGE";
constexpr const char* kEmbeddedImageEncoding = "base64-png";
constexpr uint32_t kStateMagic = 0x50575331; // PWS1
constexpr uint32_t kStateVersion = 1;
constexpr int kNumLfos = 8;
constexpr int kNumModRoutes = 32;

const std::array<const char*, 9> kSyncDivisionNames{ "1/1", "1/2", "1/4", "1/8", "1/16", "1/2T", "1/4T", "1/8T", "1/16T" };
const std::array<double, 9> kSyncDivisionBeatsPerCycle{ 4.0, 2.0, 1.0, 0.5, 0.25, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
const std::array<int, 7> kScanResolutionValues{ 32, 64, 128, 256, 512, 1024, 2048 };

const std::array<const char*, 37> kModTargetParamIds{
    "attack", "decay", "sustain", "release", "gain", "noteDrift", "liveNoteDrift",
    "scanX", "scanY", "scanLength", "scanAngle",
    "ovalX1", "ovalY1", "ovalX2", "ovalY2",
    "rectX", "rectY", "rectWidth", "rectHeight",
    "triX1", "triY1", "triX2", "triY2", "triX3", "triY3",
    "propX", "propY", "propSize", "propSpeed",
    "mapRL", "mapGL", "mapBL", "mapAL", "mapRR", "mapGR", "mapBR", "mapAR"
};

const std::array<const char*, 38> kModTargetNames{
    "None",
    "Attack", "Decay", "Sustain", "Release", "Gain", "Note Drift", "Drift Freq",
    "Line X", "Line Y", "Line Length", "Line Angle",
    "Oval X1", "Oval Y1", "Oval X2", "Oval Y2",
    "Rect X", "Rect Y", "Rect Width", "Rect Height",
    "Tri X1", "Tri Y1", "Tri X2", "Tri Y2", "Tri X3", "Tri Y3",
    "Prop X", "Prop Y", "Prop Size", "Prop Speed",
    "R->L", "G->L", "B->L", "A->L", "R->R", "G->R", "B->R", "A->R"
};

template <typename T>
T linearInterpolate(T a, T b, T t)
{
    return a + (b - a) * t;
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
}

void SineWaveVoice::prepare(double sampleRate, int samplesPerBlock, int outputChannels)
{
    juce::ignoreUnused(samplesPerBlock, outputChannels);
    currentSampleRate = sampleRate;
}

bool SineWaveVoice::canPlaySound(juce::SynthesiserSound* sound)
{
    return dynamic_cast<SineWaveSound*>(sound) != nullptr;
}

void SineWaveVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    level = velocity * 0.2f;
    const auto baseFrequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    phaseDelta = baseFrequency / currentSampleRate;

    const auto maxSemitoneOffset = juce::jlimit(0.0f, 1.0f, noteDriftAmount);
    const auto nextDriftValue = [maxSemitoneOffset]()
    {
        return juce::Random::getSystemRandom().nextFloat() * 2.0f * maxSemitoneOffset - maxSemitoneOffset;
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
    randomModulationSeed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt());
    propellorPhaseOffset = randomPropellorPhaseEnabled ? juce::Random::getSystemRandom().nextFloat() : 0.0f;
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
    if (!isVoiceActive() || waveTableLeft == nullptr || waveTableRight == nullptr || waveTableSize < 2)
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
                    const auto nextTarget = juce::Random::getSystemRandom().nextFloat() * 2.0f * maxSemitoneOffset - maxSemitoneOffset;
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

        const auto leftSample = linearInterpolate(waveTableLeft[indexA], waveTableLeft[indexB], frac);
        const auto rightSample = linearInterpolate(waveTableRight[indexA], waveTableRight[indexB], frac);

        const auto env = adsr.getNextSample();
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

void SineWaveVoice::updateAdsr(float attackMs, float decayMs, float sustainLevel, float releaseMs)
{
    adsrParams.attack = attackMs / 1000.0f;
    adsrParams.decay = decayMs / 1000.0f;
    adsrParams.sustain = sustainLevel;
    adsrParams.release = releaseMs / 1000.0f;
    adsr.setParameters(adsrParams);
}

void SineWaveVoice::setWaveTables(const float* leftTable, const float* rightTable, int size)
{
    waveTableLeft = leftTable;
    waveTableRight = rightTable;
    waveTableSize = size;
}

void SineWaveVoice::setNoteDriftAmount(float amount)
{
    noteDriftAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void SineWaveVoice::setLiveNoteDriftRateHz(float rateHz)
{
    liveNoteDriftRateHz = juce::jmax(0.0f, rateHz);
}

double SineWaveVoice::getPropellorPhaseOffset() const
{
    return static_cast<double>(propellorPhaseOffset) * juce::MathConstants<double>::twoPi;
}

uint32_t SineWaveVoice::getRandomModulationSeed() const
{
    return randomModulationSeed;
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
    synth.clearVoices();
    voices.reserve(static_cast<size_t>(kMaxVoices));
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto* voice = new SineWaveVoice();
        voices.push_back(voice);
        synth.addVoice(voice);
    }

    synth.clearSounds();
    synth.addSound(new SineWaveSound());

    perVoiceWaveTableLeft.resize(static_cast<size_t>(kMaxVoices));
    perVoiceWaveTableRight.resize(static_cast<size_t>(kMaxVoices));
    perVoiceLastScannerParams.resize(static_cast<size_t>(kMaxVoices));
    perVoiceLastPropellorPhase.resize(static_cast<size_t>(kMaxVoices), 0.0);
    perVoiceHasCachedScannerState.resize(static_cast<size_t>(kMaxVoices), false);

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

    for (auto* voice : voices)
    {
        if (voice != nullptr)
        {
            voice->prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
            voice->setAdsrSampleRate(sampleRate);
        }
    }
}

void PictureWaveSynthAudioProcessor::releaseResources()
{
    for (auto& value : modulationDisplayValues)
    {
        value.store(0.0f);
    }
    for (auto& value : effectiveDisplayValues)
    {
        value.store(0.0f);
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

    const std::array<int, 6> polyphonyChoices{ 4, 8, 12, 16, 24, 32 };
    const auto polyChoiceIndex = juce::jlimit(0, static_cast<int>(polyphonyChoices.size()) - 1,
                                              static_cast<int>(std::lround(parameters.getRawParameterValue("maxVoices")->load())));
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
        const juce::String idx = juce::String(i + 1);
        const auto rateHz = juce::jmax(0.01f, parameters.getRawParameterValue("lfo" + idx + "Rate")->load());
        const auto depth = juce::jlimit(0.0f, 1.0f, parameters.getRawParameterValue("lfo" + idx + "Depth")->load());
        const auto waveform = static_cast<int>(std::lround(parameters.getRawParameterValue("lfo" + idx + "Wave")->load()));
        const auto syncEnabled = parameters.getRawParameterValue("lfo" + idx + "Sync")->load() > 0.5f;
        const auto division = static_cast<int>(std::lround(parameters.getRawParameterValue("lfo" + idx + "Division")->load()));
        const auto randomPhasePerVoice = parameters.getRawParameterValue("lfo" + idx + "RandomPhasePerVoice")->load() > 0.5f;

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
        bool usesRandomWave = false;
        bool usesPerVoiceVariation = false;
    };

    std::array<ModRouteState, kNumModRoutes> routes{};
    std::array<float, kModTargetParamIds.size()> modulationSums{};
    modulationSums.fill(0.0f);
    for (int route = 1; route <= kNumModRoutes; ++route)
    {
        auto& routeState = routes[static_cast<size_t>(route - 1)];
        const juce::String idx = juce::String(route);
        routeState.enabled = parameters.getRawParameterValue("mod" + idx + "Enabled")->load() > 0.5f;
        if (!routeState.enabled)
        {
            continue;
        }

        routeState.source = juce::jlimit(0, kNumLfos - 1, static_cast<int>(std::lround(parameters.getRawParameterValue("mod" + idx + "Source")->load())));
        routeState.target = juce::jlimit(0, static_cast<int>(kModTargetParamIds.size()), static_cast<int>(std::lround(parameters.getRawParameterValue("mod" + idx + "Target")->load())));
        routeState.amount = juce::jlimit(-1.0f, 1.0f, parameters.getRawParameterValue("mod" + idx + "Amount")->load());
        routeState.usesRandomWave = isRandomLfoWaveform(lfoWaveforms[static_cast<size_t>(routeState.source)]);
        routeState.usesPerVoiceVariation = routeState.usesRandomWave
            || (lfoRandomPhasePerVoice[static_cast<size_t>(routeState.source)] && !routeState.usesRandomWave);

        if (routeState.target <= 0)
        {
            continue;
        }

        const auto targetIndex = routeState.target - 1;
        if (targetIndex >= static_cast<int>(kModTargetParamIds.size()))
        {
            continue;
        }

        modulationSums[static_cast<size_t>(targetIndex)] += routeState.amount * lfoValues[static_cast<size_t>(routeState.source)];
    }

    for (size_t i = 0; i < modulationSums.size(); ++i)
    {
        modulationDisplayValues[i].store(modulationSums[i]);
    }

    const auto readParam = [this, &modulationSums](const char* paramId, bool storeEffective)
    {
        auto* param = parameters.getParameter(paramId);
        auto* raw = parameters.getRawParameterValue(paramId);
        if (param == nullptr || raw == nullptr)
        {
            return 0.0f;
        }

        const auto targetIndex = modTargetIndexForParamId(paramId);
        if (targetIndex < 0)
        {
            return raw->load();
        }

        const auto baseNorm = param->getValue();
        const auto modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + modulationSums[static_cast<size_t>(targetIndex)]);
        const auto actualValue = param->convertFrom0to1(modNorm);
        if (storeEffective)
        {
            effectiveDisplayValues[static_cast<size_t>(targetIndex)].store(actualValue);
        }

        return actualValue;
    };

    const auto attack = readParam("attack", true);
    const auto decay = readParam("decay", true);
    const auto sustain = readParam("sustain", true);
    const auto release = readParam("release", true);
    const auto gainDb = readParam("gain", true);
    const auto noteDriftAmount = readParam("noteDrift", true);
    const auto liveNoteDriftHz = readParam("liveNoteDrift", true);

    ScannerParams scanner;
    scanner.x = readParam("scanX", true);
    scanner.y = readParam("scanY", true);
    scanner.length = readParam("scanLength", true);
    scanner.angleDegrees = readParam("scanAngle", true);
    scanner.scanResolution = juce::jlimit(0, static_cast<int>(kScanResolutionValues.size()) - 1,
                                          static_cast<int>(std::lround(parameters.getRawParameterValue("scanResolution")->load())));
    scanner.mode = juce::jlimit(0, 4, static_cast<int>(std::lround(parameters.getRawParameterValue("scannerMode")->load())));
    scanner.ovalX1 = readParam("ovalX1", true);
    scanner.ovalY1 = readParam("ovalY1", true);
    scanner.ovalX2 = readParam("ovalX2", true);
    scanner.ovalY2 = readParam("ovalY2", true);
    scanner.rectX = readParam("rectX", true);
    scanner.rectY = readParam("rectY", true);
    scanner.rectWidth = readParam("rectWidth", true);
    scanner.rectHeight = readParam("rectHeight", true);
    scanner.triX1 = readParam("triX1", true);
    scanner.triY1 = readParam("triY1", true);
    scanner.triX2 = readParam("triX2", true);
    scanner.triY2 = readParam("triY2", true);
    scanner.triX3 = readParam("triX3", true);
    scanner.triY3 = readParam("triY3", true);
    scanner.propX = readParam("propX", true);
    scanner.propY = readParam("propY", true);
    scanner.propSize = readParam("propSize", true);
    scanner.propSpeed = readParam("propSpeed", true);
    scanner.propSyncDivision = juce::jlimit(0, static_cast<int>(kSyncDivisionNames.size()) - 1, static_cast<int>(std::lround(parameters.getRawParameterValue("propSyncDivision")->load())));
    scanner.propTempoSync = parameters.getRawParameterValue("propTempoSync")->load() > 0.5f;
    const auto randomPhaseEnabled = parameters.getRawParameterValue("randomPhase")->load() > 0.5f;
    scanner.mapRL = readParam("mapRL", true);
    scanner.mapGL = readParam("mapGL", true);
    scanner.mapBL = readParam("mapBL", true);
    scanner.mapAL = readParam("mapAL", true);
    scanner.mapRR = readParam("mapRR", true);
    scanner.mapGR = readParam("mapGR", true);
    scanner.mapBR = readParam("mapBR", true);
    scanner.mapAR = readParam("mapAR", true);

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

    const auto basePropellorPhase = propellorPhase.load();
    bool displayedVoiceValues = false;
    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (auto* voice = voices[static_cast<size_t>(i)])
        {
            if (i >= maxVoices)
            {
                voice->forceStop();
                continue;
            }

            std::array<float, kModTargetParamIds.size()> voiceModulationSums = modulationSums;
            if (hasPerVoiceVariableRoutes)
            {
                voiceModulationSums.fill(0.0f);
                for (const auto& route : routes)
                {
                    if (!route.enabled)
                    {
                        continue;
                    }

                    auto sourceValue = lfoValues[static_cast<size_t>(route.source)];
                    if (route.usesRandomWave)
                    {
                        sourceValue = renderExtendedLfoSample(
                            lfoWaveforms[static_cast<size_t>(route.source)],
                            route.source,
                            lfoCyclePositions[static_cast<size_t>(route.source)],
                            lfoPhases[static_cast<size_t>(route.source)],
                            voice->getRandomModulationSeed()) * lfoDepths[static_cast<size_t>(route.source)];
                    }
                    else if (lfoRandomPhasePerVoice[static_cast<size_t>(route.source)])
                    {
                        const auto offsetPhase = lfoPhases[static_cast<size_t>(route.source)]
                            + phaseOffsetFromVoiceSeed(route.source, voice->getRandomModulationSeed());
                        sourceValue = renderLfoSample(lfoWaveforms[static_cast<size_t>(route.source)], offsetPhase)
                            * lfoDepths[static_cast<size_t>(route.source)];
                    }

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
            }

            const auto readVoiceParam = [this, &voiceModulationSums](const char* paramId)
            {
                auto* param = parameters.getParameter(paramId);
                auto* raw = parameters.getRawParameterValue(paramId);
                if (param == nullptr || raw == nullptr)
                {
                    return 0.0f;
                }

                const auto targetIndex = modTargetIndexForParamId(paramId);
                if (targetIndex < 0)
                {
                    return raw->load();
                }

                const auto baseNorm = param->getValue();
                const auto modNorm = juce::jlimit(0.0f, 1.0f, baseNorm + voiceModulationSums[static_cast<size_t>(targetIndex)]);
                return param->convertFrom0to1(modNorm);
            };

            const auto voiceAttack = hasPerVoiceVariableRoutes ? readVoiceParam("attack") : attack;
            const auto voiceDecay = hasPerVoiceVariableRoutes ? readVoiceParam("decay") : decay;
            const auto voiceSustain = hasPerVoiceVariableRoutes ? readVoiceParam("sustain") : sustain;
            const auto voiceNoteDrift = hasPerVoiceVariableRoutes ? readVoiceParam("noteDrift") : noteDriftAmount;
            const auto voiceLiveNoteDrift = hasPerVoiceVariableRoutes ? readVoiceParam("liveNoteDrift") : liveNoteDriftHz;

            voice->updateAdsr(voiceAttack, voiceDecay, voiceSustain, hasPerVoiceVariableRoutes ? readVoiceParam("release") : release);
            voice->setNoteDriftAmount(voiceNoteDrift);
            voice->setLiveNoteDriftRateHz(voiceLiveNoteDrift);
            voice->setRandomPropellorPhaseEnabled(usePerVoicePropellorPhase);

            if (!displayedVoiceValues && voice->isVoiceActive() && hasPerVoiceVariableRoutes)
            {
                for (size_t targetIndex = 0; targetIndex < kModTargetParamIds.size(); ++targetIndex)
                {
                    modulationDisplayValues[targetIndex].store(voiceModulationSums[targetIndex]);
                    auto* param = parameters.getParameter(kModTargetParamIds[targetIndex]);
                    if (param != nullptr)
                    {
                        const auto actualValue = param->convertFrom0to1(
                            juce::jlimit(0.0f, 1.0f, param->getValue() + voiceModulationSums[targetIndex]));
                        effectiveDisplayValues[targetIndex].store(actualValue);
                    }
                }

                displayedVoiceValues = true;
            }

            if (usePerVoiceScannerState)
            {
                auto& left = perVoiceWaveTableLeft[static_cast<size_t>(i)];
                auto& right = perVoiceWaveTableRight[static_cast<size_t>(i)];
                auto voiceScanner = scanner;
                if (hasPerVoiceVariableScannerRoutes)
                {
                    voiceScanner.x = readVoiceParam("scanX");
                    voiceScanner.y = readVoiceParam("scanY");
                    voiceScanner.length = readVoiceParam("scanLength");
                    voiceScanner.angleDegrees = readVoiceParam("scanAngle");
                    voiceScanner.ovalX1 = readVoiceParam("ovalX1");
                    voiceScanner.ovalY1 = readVoiceParam("ovalY1");
                    voiceScanner.ovalX2 = readVoiceParam("ovalX2");
                    voiceScanner.ovalY2 = readVoiceParam("ovalY2");
                    voiceScanner.rectX = readVoiceParam("rectX");
                    voiceScanner.rectY = readVoiceParam("rectY");
                    voiceScanner.rectWidth = readVoiceParam("rectWidth");
                    voiceScanner.rectHeight = readVoiceParam("rectHeight");
                    voiceScanner.triX1 = readVoiceParam("triX1");
                    voiceScanner.triY1 = readVoiceParam("triY1");
                    voiceScanner.triX2 = readVoiceParam("triX2");
                    voiceScanner.triY2 = readVoiceParam("triY2");
                    voiceScanner.triX3 = readVoiceParam("triX3");
                    voiceScanner.triY3 = readVoiceParam("triY3");
                    voiceScanner.propX = readVoiceParam("propX");
                    voiceScanner.propY = readVoiceParam("propY");
                    voiceScanner.propSize = readVoiceParam("propSize");
                    voiceScanner.propSpeed = readVoiceParam("propSpeed");
                    voiceScanner.mapRL = readVoiceParam("mapRL");
                    voiceScanner.mapGL = readVoiceParam("mapGL");
                    voiceScanner.mapBL = readVoiceParam("mapBL");
                    voiceScanner.mapAL = readVoiceParam("mapAL");
                    voiceScanner.mapRR = readVoiceParam("mapRR");
                    voiceScanner.mapGR = readVoiceParam("mapGR");
                    voiceScanner.mapBR = readVoiceParam("mapBR");
                    voiceScanner.mapAR = readVoiceParam("mapAR");
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
                }
                else if (scannerChanged || !perVoiceHasCachedScannerState[static_cast<size_t>(i)])
                {
                    generateFallbackWaveTables(left.data(), right.data());
                    perVoiceLastScannerParams[static_cast<size_t>(i)] = voiceScanner;
                    perVoiceLastPropellorPhase[static_cast<size_t>(i)] = phaseValue;
                    perVoiceHasCachedScannerState[static_cast<size_t>(i)] = true;
                }

                voice->setWaveTables(left.data(), right.data(), kWaveTableSize);
            }
            else
            {
                voice->setWaveTables(waveTableLeft.data(), waveTableRight.data(), kWaveTableSize);
            }
        }
    }

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

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
        "gain", "Gain", juce::NormalisableRange<float>(-36.0f, 0.0f, 0.1f), -12.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "noteDrift", "Note Drift", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "liveNoteDrift", "Drift Freq", juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "maxVoices", "Polyphony", juce::StringArray{ "4", "8", "12", "16", "24", "32" }, 3));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "scanX", "Line X", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "scanY", "Line Y", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "scanLength", "Line Length", juce::NormalisableRange<float>(0.05f, 1.5f, 0.001f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "scanAngle", "Line Angle", juce::NormalisableRange<float>(-180.0f, 180.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scanResolution", "Scan Resolution", juce::StringArray{ "32", "64", "128", "256", "512", "1024", "2048" }, 1));
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
        "rectX", "Rect X", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectY", "Rect Y", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectWidth", "Rect Width", juce::NormalisableRange<float>(0.05f, 1.5f, 0.001f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rectHeight", "Rect Height", juce::NormalisableRange<float>(0.05f, 1.5f, 0.001f), 0.3f));

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
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "mod" + idx + "Amount", "Mod " + idx + " Amount", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapRL", "R -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapGL", "G -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapBL", "B -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), -1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapAL", "A -> L", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapRR", "R -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapGR", "G -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapBR", "B -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "mapAR", "A -> R", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 1.0f));

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

bool PictureWaveSynthAudioProcessor::hasLoadedImage() const
{
    const juce::SpinLock::ScopedLockType lock(imageLock);
    return loadedImageData != nullptr;
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

    lastScannerParams = scanner;
    hasLastScannerParams = true;
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
        const auto indexB = juce::jlimit(0, sampleCount - 1, indexA + 1);
        const auto frac = sourcePos - static_cast<float>(indexA);
        outLeft[static_cast<size_t>(i)] = linearInterpolate(sampledLeft[static_cast<size_t>(indexA)], sampledLeft[static_cast<size_t>(indexB)], frac);
        outRight[static_cast<size_t>(i)] = linearInterpolate(sampledRight[static_cast<size_t>(indexA)], sampledRight[static_cast<size_t>(indexB)], frac);

    }

    const auto meanLeft = sumLeft / static_cast<float>(kWaveTableSize);
    const auto meanRight = sumRight / static_cast<float>(kWaveTableSize);

    float maxAbs = 0.0001f;
    for (int i = 0; i < kWaveTableSize; ++i)
    {
        outLeft[static_cast<size_t>(i)] -= meanLeft;
        outRight[static_cast<size_t>(i)] -= meanRight;

        maxAbs = juce::jmax(maxAbs, std::abs(outLeft[static_cast<size_t>(i)]));
        maxAbs = juce::jmax(maxAbs, std::abs(outRight[static_cast<size_t>(i)]));
    }

    const auto normalise = 0.9f / maxAbs;
    for (int i = 0; i < kWaveTableSize; ++i)
    {
        outLeft[static_cast<size_t>(i)] *= normalise;
        outRight[static_cast<size_t>(i)] *= normalise;
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

        return { cx + rx * std::cos(theta), cy + ry * std::sin(theta) };
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

        const auto edgeT = tt * 4.0f;
        if (edgeT < 1.0f)
        {
            return { linearInterpolate(x0, x1, edgeT), y0 };
        }
        if (edgeT < 2.0f)
        {
            return { x1, linearInterpolate(y0, y1, edgeT - 1.0f) };
        }
        if (edgeT < 3.0f)
        {
            return { linearInterpolate(x1, x0, edgeT - 2.0f), y1 };
        }

        return { x0, linearInterpolate(y1, y0, edgeT - 3.0f) };
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

    const auto angleRadians = juce::degreesToRadians(scanner.angleDegrees);
    const auto dx = std::cos(angleRadians) * scanner.length;
    const auto dy = std::sin(angleRadians) * scanner.length;
    const auto startX = scanner.x - 0.5f * static_cast<float>(dx);
    const auto startY = scanner.y - 0.5f * static_cast<float>(dy);
    return { startX + static_cast<float>(dx) * tt, startY + static_cast<float>(dy) * tt };
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
        && juce::approximatelyEqual(a.x, b.x)
        && juce::approximatelyEqual(a.y, b.y)
        && juce::approximatelyEqual(a.length, b.length)
        && juce::approximatelyEqual(a.angleDegrees, b.angleDegrees)
        && juce::approximatelyEqual(a.ovalX1, b.ovalX1)
        && juce::approximatelyEqual(a.ovalY1, b.ovalY1)
        && juce::approximatelyEqual(a.ovalX2, b.ovalX2)
        && juce::approximatelyEqual(a.ovalY2, b.ovalY2)
        && juce::approximatelyEqual(a.rectX, b.rectX)
        && juce::approximatelyEqual(a.rectY, b.rectY)
        && juce::approximatelyEqual(a.rectWidth, b.rectWidth)
        && juce::approximatelyEqual(a.rectHeight, b.rectHeight)
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
