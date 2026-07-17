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

juce_add_binary_data(PhotoSynthesisAssets
  SOURCES
    src/assets/init.pspreset
)

juce_add_plugin(PhotoSynthesis
  COMPANY_NAME "Philip Bergwerf"
  BUNDLE_ID "com.philipbergwerf.photosynthesis"
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
    PhotoSynthesisAssets
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  # Silence recurring non-critical warnings that are currently intentional/known.
  target_compile_options(PhotoSynthesis PRIVATE
    -Wno-float-equal
    -Wno-shadow
    -Wno-deprecated-declarations
  )
endif()

juce_generate_juce_header(PhotoSynthesis)

if(APPLE)
  add_custom_command(TARGET PhotoSynthesis_Standalone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/Applications"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$ENV{HOME}/Applications/PhotoSynthesis.app"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "$<TARGET_BUNDLE_DIR:PhotoSynthesis_Standalone>"
      "$ENV{HOME}/Applications/PhotoSynthesis.app"
    COMMENT "Copying PhotoSynthesis Standalone to ~/Applications"
  )
endif()
