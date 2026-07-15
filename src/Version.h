#pragma once

// Single source of truth for plugin version metadata.
namespace PhotoSynthesisVersion
{
constexpr const char* kVersion = "0.0.1";
constexpr const char* kDisplayName = "PhotoSynthesis";
constexpr const char* kAuthor = "Philip Bergwerf";
constexpr const char* kEmail = "philipbergwerf@gmail.com";
}

/* 
Changelog (latest first)
0.0.1
- Initial public baseline.
- Added modulation matrix with multiple source types.
- Added scanner tab layout and routing settings tab.
- Added polyphonic velocity source handling and modulation response smoothing.
- Added balanced default RGB mapping profile and disabled alpha defaults.
*/