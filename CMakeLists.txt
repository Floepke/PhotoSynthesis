cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0175)
  cmake_policy(SET CMP0175 OLD)
  set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)
endif()

project(PhotoSynthesis VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.2
)

FetchContent_MakeAvailable(juce)

juce_add_plugin(PhotoSynthesis
  COMPANY_NAME "wavpic"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  PLUGIN_MANUFACTURER_CODE WvPc
  PLUGIN_CODE PwS1
  FORMATS VST3 Standalone
  PRODUCT_NAME "PhotoSynthesis"
)

target_sources(PhotoSynthesis
  PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
)

target_compile_definitions(PhotoSynthesis
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(PhotoSynthesis
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

juce_generate_juce_header(PhotoSynthesis)
