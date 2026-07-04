#pragma once

#include <Arduino.h>

#include "settings.h"

struct ColorSchemeDefinition {
  ColorScheme id;
  const char *name;
  uint32_t incomingRgb;
  uint32_t outgoingRgb;
};

#if defined(HW_AMOLED_143)
constexpr uint32_t DEFAULT_INCOMING_RGB = 0x00FF40;
constexpr uint32_t DEFAULT_OUTGOING_RGB = 0x0080FF;
#else
constexpr uint32_t DEFAULT_INCOMING_RGB = 0xADFF2F;
constexpr uint32_t DEFAULT_OUTGOING_RGB = 0xFFA500;
#endif

constexpr ColorSchemeDefinition COLOR_SCHEME_DEFINITIONS[] = {
    {ColorScheme::Default, "Default",
     DEFAULT_INCOMING_RGB, DEFAULT_OUTGOING_RGB},
    {ColorScheme::Cyber, "Cyber", 0x00D9FF, 0xFF2D95},
    {ColorScheme::Ice, "Ice", 0x38BDF8, 0xFB7185},
    {ColorScheme::Lime, "Lime", 0xA3E635, 0xA78BFA},
    {ColorScheme::Azure, "Azure", 0x3B82F6, 0xFBBF24},
    {ColorScheme::Mint, "Mint", 0x2DD4BF, 0xFF6B4A},
    {ColorScheme::Blue, "Blue", 0x56B4E9, 0xE69F00},
    {ColorScheme::Violet, "Violet", 0x8B5CF6, 0xFDE047},
};

constexpr size_t COLOR_SCHEME_DEFINITION_COUNT =
    sizeof(COLOR_SCHEME_DEFINITIONS) /
    sizeof(COLOR_SCHEME_DEFINITIONS[0]);

inline const ColorSchemeDefinition *findColorSchemeDefinition(
    ColorScheme scheme) {
  for (size_t i = 0; i < COLOR_SCHEME_DEFINITION_COUNT; ++i) {
    if (COLOR_SCHEME_DEFINITIONS[i].id == scheme) {
      return &COLOR_SCHEME_DEFINITIONS[i];
    }
  }
  return nullptr;
}
