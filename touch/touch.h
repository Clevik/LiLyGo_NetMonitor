#pragma once

#include <Arduino.h>

bool touchInit(uint16_t displayRotation);
void touchSetRotation(uint16_t displayRotation);

#if defined(HW_TOUCH_CST9217)
bool touchReadTap(int16_t &x, int16_t &y);
#else
bool touchButtonPressed();
#endif
