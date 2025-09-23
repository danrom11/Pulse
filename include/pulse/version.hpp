#pragma once

#define PULSE_VERSION_MAJOR 0
#define PULSE_VERSION_MINOR 1
#define PULSE_VERSION_PATCH 0

#define PULSE_VERSION_CODE \
  ((PULSE_VERSION_MAJOR << 16) | (PULSE_VERSION_MINOR << 8) | (PULSE_VERSION_PATCH))

#define PULSE_VERSION_STRING "0.1.0"

namespace pulse {
struct version {
  static constexpr int major = PULSE_VERSION_MAJOR;
  static constexpr int minor = PULSE_VERSION_MINOR;
  static constexpr int patch = PULSE_VERSION_PATCH;
  static constexpr const char* string = PULSE_VERSION_STRING;
};
}
