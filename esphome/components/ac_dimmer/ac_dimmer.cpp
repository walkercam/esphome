#include "ac_dimmer.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <cmath>
#include <numbers>

#include "hw_timer_esp_idf.h"

namespace esphome::ac_dimmer {

static const char *const TAG = "ac_dimmer";

// Global array to store dimmer objects
static AcDimmerDataStore *all_dimmers[32];  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
// timer callback for the next scheduled event; re-arms for the following one-shot alarm.
static HWTimer *dimmer_timer = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/// Time in microseconds the gate should be held high
/// 10µs should be long enough for most triacs
/// For reference: BT136 datasheet says 2µs nominal (page 7)
/// However other factors like gate driver propagation time
/// are also considered and a really low value is not important
/// See also: https://github.com/esphome/issues/issues/1632
static constexpr uint32_t GATE_ENABLE_TIME = 50;

/// Timer frequency in Hz (1 MHz = 1µs resolution)
static constexpr uint32_t TIMER_FREQUENCY_HZ = 1000000;

/// Arm the next pending dimmer event in one-shot alarm mode.
static void IRAM_ATTR HOT schedule_next_timer_alarm() {
  if (dimmer_timer == nullptr)
    return;

  uint64_t next_timer_count = UINT64_MAX;

  for (auto *dimmer : all_dimmers) {
    if (dimmer == nullptr) {
      break;
    }
    if (dimmer->crossed_zero_at == 0)
      continue;

    if (dimmer->enable_time_us != 0) {
      next_timer_count = dimmer->enable_time_us;
      //if (event_time >= (now-5) && event_time < next_deadline_us) {
      //  next_deadline_us = event_time;
      //}
    }
    if (dimmer->disable_time_us != 0 && dimmer->disable_time_us < next_timer_count) {
      next_timer_count = dimmer->disable_time_us;
      //if (event_time >= (now-5) && event_time < next_deadline_us) {
      //  next_deadline_us = event_time;
      //}
    }
  }

  if (next_timer_count == UINT64_MAX)
    return;
/*
  uint64_t delta = next_deadline_us > now ? (next_deadline_us - now) : 0;
  // Ensure at least 1us delay to avoid re-triggering immediately
  if (delta < 1)
    delta = 1;    //see if 1us minimum gives reliable triggering. we may miss some events
*/

  timer_alarm_direct_one_shot(dimmer_timer, next_timer_count);

}

/// GPIO interrupt routine, called when ZC pin triggers
void IRAM_ATTR HOT AcDimmerDataStore::gpio_intr(uint8_t edge) {
  uint64_t now = timer_get_raw_count(dimmer_timer);
  uint64_t prev_crossed = this->crossed_zero_at;

  if ((now - prev_crossed) < 9000) {
    return;
  }

  this->crossed_zero_at = now;
  this->cycle_time_us = this->crossed_zero_at - prev_crossed;

  if (this->value == 65535) {
    // fully on, enable output immediately
    this->gate_pin.digital_write(true);
    this->enable_time_us = 0;
    this->disable_time_us = 0;
  } else if (this->init_cycle) {
    // send a full cycle
    this->init_cycle = this->init_cycle - 1;
    this->enable_time_us = 1;
    this->disable_time_us = this->cycle_time_us;
  } else if (this->value == 0) {
    // fully off, disable output immediately
    this->gate_pin.digital_write(false);
    this->enable_time_us = 0;
    this->disable_time_us = 0;
  } else {
    auto min_us = this->cycle_time_us * this->min_power / 1000;
    if (this->method == DIM_METHOD_TRAILING) {
      if (edge) {
        this->enable_time_us = now + 0;
        this->disable_time_us = (this->value * 10000 / 65535) + now + 0;
      } else {
        this->enable_time_us = now + 64;
        this->disable_time_us = (this->value * 10000 / 65535) + now + 64;
      }
      // Calculate time until disable in µs with integer arithmetic and take into account min_power
      //this->disable_time_us = std::max((uint64_t) 10, this->value * (this->cycle_time_us - min_us) / 65535 + min_us) + now;
    } else {
      // Calculate time until enable in µs: (1.0-value)*cycle_time, but with integer arithmetic
      // also take into account min_power
      this->enable_time_us = ((65535 - this->value) * 10000 / 65535) + now; 

      if (this->method == DIM_METHOD_LEADING_PULSE) {
        // Minimum pulse time should be enough for the triac to trigger when it is close to the ZC zone
        // this is for brightness near 99%
        this->disable_time_us = std::max(this->enable_time_us + GATE_ENABLE_TIME, (uint64_t) cycle_time_us / 10);
      } else {
        if (edge) {
          this->disable_time_us = now + 0;
        } else {
          this->disable_time_us = now + 64;
          this->enable_time_us = this->enable_time_us + 64;
        }      
      }
    }
  }

  schedule_next_timer_alarm();

  // Create debug record for this half-cycle after requested times are calculated.
  //uint8_t idx = this->dbg_write_idx;
  //this->debug_buffer[idx].zc_timestamp = (uint32_t)now; //this->crossed_zero_at;
  //this->debug_buffer[idx].zc_period_us = 0; //this->cycle_time_us;
  //this->debug_buffer[idx].requested_on_us = 0; //this->enable_time_us;
  //this->debug_buffer[idx].actual_on_us = 0; //micros();                  // grab the actual on time here with micros so we can see if there is a delay between the zc and the actual on time.
  //this->debug_buffer[idx].requested_off_us = (uint32_t)this->disable_time_us;
  //this->debug_buffer[idx].actual_off_us = 0;
  //this->dbg_active_idx = idx;
  //this->dbg_write_idx = (uint8_t)((idx + 1) & DEBUG_BUF_MASK);
}

void IRAM_ATTR HOT AcDimmerDataStore::s_gpio_intr(AcDimmerDataStore *store) {
  // Attaching pin interrupts on the same pin will override the previous interrupt
  // However, the user expects that multiple dimmers sharing the same ZC pin will work.
  // We solve this in a bit of a hacky way: On each pin interrupt, we check all dimmers
  // if any of them are using the same ZC pin, and also trigger the interrupt for *them*.
  uint8_t edge = store->zero_cross_pin.digital_read();
  
  for (auto *dimmer : all_dimmers) {
    if (dimmer == nullptr)
      break;
    if (dimmer->zero_cross_pin_number == store->zero_cross_pin_number) {
      dimmer->gpio_intr(edge);
    }
  }
}

void IRAM_ATTR HOT AcDimmerDataStore::s_timer_intr() {
  //get current GPTimer count
  uint64_t cur_count = timer_get_raw_count(dimmer_timer);
  
  for (auto *dimmer : all_dimmers) {
    if (dimmer == nullptr) {
      break;
    }

    if (dimmer->crossed_zero_at == 0)
      continue;

    if (dimmer->enable_time_us != 0 && cur_count >= dimmer->enable_time_us) {
      dimmer->enable_time_us = 0;
      dimmer->gate_pin.digital_write(true);
    }

    if (dimmer->disable_time_us != 0 && cur_count >= dimmer->disable_time_us) {
      dimmer->disable_time_us = 0;
      dimmer->gate_pin.digital_write(false);
    }
    
    /*
    uint8_t idx = dimmer->dbg_active_idx;
    dimmer->debug_buffer[idx].requested_on_us = cur_count; // - dimmer->debug_buffer[idx].requested_on_us;
    dimmer->debug_buffer[idx].actual_on_us = now - dimmer->debug_buffer[idx].actual_on_us;
    dimmer->dbg_write_idx = (uint8_t)((idx + 1) & DEBUG_BUF_MASK);
    */
  }

  schedule_next_timer_alarm();
}

void AcDimmer::setup() {
  // extend all_dimmers array with our dimmer

  // Need to be sure the zero cross pin is setup only once, ESP8266 fails and ESP32 seems to fail silently
  auto setup_zero_cross_pin = true;

  for (auto &all_dimmer : all_dimmers) {
    if (all_dimmer == nullptr) {
      all_dimmer = &this->store_;
      break;
    }
    if (all_dimmer->zero_cross_pin_number == this->zero_cross_pin_->get_pin()) {
      setup_zero_cross_pin = false;
    }
  }

  this->gate_pin_->setup();
  this->store_.gate_pin = this->gate_pin_->to_isr();
  this->store_.zero_cross_pin_number = this->zero_cross_pin_->get_pin();
  this->store_.min_power = static_cast<uint16_t>(this->min_power_ * 1000);
  this->min_power_ = 0;
  this->store_.method = this->method_;

  if (setup_zero_cross_pin) {
    this->zero_cross_pin_->setup();
    this->store_.zero_cross_pin = this->zero_cross_pin_->to_isr();
    this->zero_cross_pin_->attach_interrupt(&AcDimmerDataStore::s_gpio_intr, &this->store_,
                                            this->zero_cross_interrupt_type_);
  }

  if (dimmer_timer == nullptr) {
    dimmer_timer = timer_begin(TIMER_FREQUENCY_HZ);
    if (dimmer_timer == nullptr) {
      ESP_LOGE(TAG, "Failed to create GPTimer for AC dimmer");
      this->mark_failed();
      return;
    }
    timer_attach_interrupt(dimmer_timer, &AcDimmerDataStore::s_timer_intr);
  }
}

void AcDimmer::write_state(float state) {
  //state = std::acos(1 - (2 * state)) / std::numbers::pi_v<float>;  // RMS power compensation
  auto new_value = static_cast<uint16_t>(roundf(state * 65535));
  if (new_value != 0 && this->store_.value == 0)
    this->store_.init_cycle = this->init_with_half_cycle_ * 5;  //change the multiplier to change how many half cycles to send at turn on
  this->store_.value = new_value;
}

void AcDimmer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "AcDimmer:\n"
                "  Min Power: %.1f%%\n"
                "  Init with half cycle: %s",
                this->store_.min_power / 10.0f, YESNO(this->init_with_half_cycle_));
  LOG_PIN("  Output Pin: ", this->gate_pin_);
  LOG_PIN("  Zero-Cross Pin: ", this->zero_cross_pin_);
  if (this->zero_cross_interrupt_type_ == gpio::INTERRUPT_RISING_EDGE) {
    ESP_LOGCONFIG(TAG, "  Interrupt Type: rising");
  } else if (this->zero_cross_interrupt_type_ == gpio::INTERRUPT_FALLING_EDGE) {
    ESP_LOGCONFIG(TAG, "  Interrupt Type: falling");
  } else {
    ESP_LOGCONFIG(TAG, "  Interrupt Type: any");
  }
  if (method_ == DIM_METHOD_LEADING_PULSE) {
    ESP_LOGCONFIG(TAG, "  Method: leading pulse");
  } else if (method_ == DIM_METHOD_LEADING) {
    ESP_LOGCONFIG(TAG, "  Method: leading");
  } else {
    ESP_LOGCONFIG(TAG, "  Method: trailing");
  }
  LOG_FLOAT_OUTPUT(this);
  ESP_LOGV(TAG, "  Estimated Frequency: %.3fHz", 1e6f / this->store_.cycle_time_us / 2);
}

void AcDimmer::loop() {
  uint32_t now = millis();
  if (now - this->last_log_time_ < 1000)
    return;
  this->last_log_time_ = now;

  // Drain completed debug records and log CSV lines
  /*
  while (this->store_.dbg_read_idx != this->store_.dbg_write_idx) {
    uint8_t idx = this->store_.dbg_read_idx;
    // Copy to local to avoid races while logging
    AcDimmerDataStore::DebugEvent ev = this->store_.debug_buffer[idx];
    ESP_LOGD(TAG, "%lu,%lu,%lu,%lu,%lu,%lu", ev.zc_timestamp, ev.zc_period_us,
             ev.requested_on_us, ev.actual_on_us, ev.requested_off_us, ev.actual_off_us);
    this->store_.dbg_read_idx = (uint8_t)((idx + 1) & AcDimmerDataStore::DEBUG_BUF_MASK);
  }
    */
}

}  // namespace esphome::ac_dimmer
