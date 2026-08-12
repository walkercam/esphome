#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/output/float_output.h"

namespace esphome::ac_dimmer {

enum DimMethod { DIM_METHOD_LEADING_PULSE = 0, DIM_METHOD_LEADING, DIM_METHOD_TRAILING };

struct AcDimmerDataStore {
  /// Zero-cross pin
  ISRInternalGPIOPin zero_cross_pin;
  /// Zero-cross pin number - used to share ZC pin across multiple dimmers
  uint8_t zero_cross_pin_number;
  /// Output pin to write to
  ISRInternalGPIOPin gate_pin;
  /// Value of the dimmer - 0 to 65535.
  uint16_t value;
  /// Minimum power for activation
  uint16_t min_power;
  /// Time between the last two ZC pulses
  uint32_t cycle_time_us;
  /// Time (in micros()) of last ZC signal
  uint32_t crossed_zero_at;
  /// Time since last ZC pulse to enable gate pin. 0 means not set.
  uint32_t enable_time_us;
  /// Time since last ZC pulse to disable gate pin. 0 means no disable.
  uint32_t disable_time_us;
  /// Set to send the first half ac cycle complete
  bool init_cycle;
  /// Dimmer method
  DimMethod method;

  uint32_t timer_intr(uint32_t now);

  void gpio_intr();
  static void s_gpio_intr(AcDimmerDataStore *store);
#ifdef USE_ESP32
  static void s_timer_intr();
#endif
  // Debug event record for each half-cycle
  struct DebugEvent {
    uint32_t zc_timestamp;
    uint32_t zc_period_us;
    uint32_t requested_on_us;
    uint32_t actual_on_us;
    uint32_t requested_off_us;
    uint32_t actual_off_us;
  };

  static constexpr size_t DEBUG_BUF_SIZE = 256;
  static constexpr uint8_t DEBUG_BUF_MASK = 0xFF;  // DEBUG_BUF_SIZE - 1
  // Ring buffer of debug events (written from ISR, read from loop())
  DebugEvent debug_buffer[DEBUG_BUF_SIZE];
  volatile uint8_t dbg_write_idx{0};
  volatile uint8_t dbg_read_idx{0};
  // Index of the currently active record (set at ZC, updated by timer ISR)
  volatile uint8_t dbg_active_idx{0};
};

class AcDimmer final : public output::FloatOutput, public Component {
 public:
  void setup() override;
  void loop() override;

  void dump_config() override;
  void set_gate_pin(InternalGPIOPin *gate_pin) { gate_pin_ = gate_pin; }
  void set_zero_cross_pin(InternalGPIOPin *zero_cross_pin) { zero_cross_pin_ = zero_cross_pin; }
  void set_zero_cross_interrupt_type(gpio::InterruptType type) { zero_cross_interrupt_type_ = type; }
  void set_init_with_half_cycle(bool init_with_half_cycle) { init_with_half_cycle_ = init_with_half_cycle; }
  void set_method(DimMethod method) { method_ = method; }

 protected:
  void write_state(float state) override;

  InternalGPIOPin *gate_pin_;
  InternalGPIOPin *zero_cross_pin_;
  gpio::InterruptType zero_cross_interrupt_type_;
  AcDimmerDataStore store_;
  bool init_with_half_cycle_;
  DimMethod method_;
  // For periodic logging in loop()
  uint32_t last_log_time_{0};
};

}  // namespace esphome::ac_dimmer
