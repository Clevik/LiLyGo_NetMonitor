#pragma once

// ============================================================
//  Слой абстракции железа.
//  Выбирает конфигурацию платы на этапе компиляции по макросу,
//  передаваемому из platformio.ini (-DHW_AMOLED_191 / -DHW_AMOLED_143).
// ============================================================

#if defined(HW_AMOLED_191)
  #include "hardware/amoled_191.h"
#elif defined(HW_AMOLED_143)
  #include "hardware/amoled_143.h"
#else
  #error "Не задан вариант платы. Определите HW_AMOLED_191 или HW_AMOLED_143 в build_flags."
#endif
