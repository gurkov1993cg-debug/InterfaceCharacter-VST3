#pragma once

#include <cstdint>

namespace interface_character {

enum class ProfileId : std::uint8_t {
    UniversalAudioApollo = 0,
    ProToolsTdm88824,
    ProToolsHdxHdIo,
    PrismSoundLyra1,
};

struct ProfileSpec {
    ProfileId id;
    const char* shortName;
    const char* description;

    float lowShelfDb;
    float highShelfDb;
    float lowShelfHz;
    float highShelfHz;

    float saturation;
    float secondHarmonic;
    float thirdHarmonic;
    float asymmetry;
    float memory;
    float crossfeed;
    int quantizationBits;
    float clipThresholdDb;
};

const ProfileSpec& getProfileSpec(ProfileId id) noexcept;

} // namespace interface_character
