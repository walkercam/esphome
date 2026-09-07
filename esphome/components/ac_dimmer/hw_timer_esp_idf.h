#pragma once
#ifdef USE_ESP32

#include "driver/gptimer_types.h"

namespace esphome::ac_dimmer {

struct HWTimer;

HWTimer *timer_begin(uint32_t frequency);

void timer_attach_interrupt(HWTimer *timer, void (*user_func)());
void timer_alarm(HWTimer *timer, uint64_t alarm_value, bool autoreload, uint64_t reload_count);
void timer_alarm_direct_one_shot(HWTimer *timer, uint64_t alarm_count);
uint64_t timer_get_raw_count(HWTimer *timer);

}  // namespace esphome::ac_dimmer

#endif
