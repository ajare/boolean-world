#pragma once

#include <optional>
#include <string_view>

#include <willpower/application/AudioOptions.h>

namespace bw::app {

// Player-facing audio output device (Game.yaml's Audio/Output). The program
// cannot detect whether a stereo device is headphones or desk speakers, so it
// asks: Headphones forces stereo and enables HRTF, a technique that only
// works with exactly two channels reaching two ears; Speakers follows
// whatever the OS reports for the active device; Surround asks FMOD for a
// fixed 5.1 mix.
enum class AudioOutput {
  Headphones,
  Speakers,
  Surround,
};

constexpr int audioOutputCode(AudioOutput output) {
  return static_cast<int>(output);
}

constexpr std::optional<AudioOutput> audioOutputFromCode(int code) {
  switch (code) {
    case audioOutputCode(AudioOutput::Headphones):
      return AudioOutput::Headphones;
    case audioOutputCode(AudioOutput::Speakers):
      return AudioOutput::Speakers;
    case audioOutputCode(AudioOutput::Surround):
      return AudioOutput::Surround;
    default:
      return std::nullopt;
  }
}

constexpr std::string_view audioOutputName(AudioOutput output) {
  switch (output) {
    case AudioOutput::Headphones:
      return "Headphones";
    case AudioOutput::Speakers:
      return "Speakers";
    case AudioOutput::Surround:
      return "Surround";
  }
  return "";
}

constexpr std::optional<AudioOutput> audioOutputFromName(
    std::string_view name) {
  if (name == "headphones") return AudioOutput::Headphones;
  if (name == "speakers") return AudioOutput::Speakers;
  if (name == "surround") return AudioOutput::Surround;
  return std::nullopt;
}

// HRTF is a headphone-only technique: it synthesises the interaural cues two
// ears would receive, so it needs exactly two output channels reaching them
// and sounds worse than a plain mix on speakers.
constexpr bool audioOutputEnablesHrtf(AudioOutput output) {
  return output == AudioOutput::Headphones;
}

constexpr wp::application::SpeakerMode audioOutputSpeakerMode(
    AudioOutput output) {
  switch (output) {
    case AudioOutput::Headphones:
      return wp::application::SpeakerMode::Stereo;
    case AudioOutput::Surround:
      return wp::application::SpeakerMode::Surround5Point1;
    case AudioOutput::Speakers:
      break;
  }
  return wp::application::SpeakerMode::Default;
}

}  // namespace bw::app
