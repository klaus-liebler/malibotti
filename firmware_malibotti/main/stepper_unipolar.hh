#pragma once

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_gpio.h"

// Header-only control for a single 4-phase unipolar stepper.
// The phase sequence matches the Arduino Stepper library's 4-wire stepping pattern.
template <gpio_num_t PH1,
          gpio_num_t PH2,
          gpio_num_t PH3,
          gpio_num_t PH4,
          uint16_t STEPS_PER_REV,
          bool DIRECTION_INVERTED,
          bool USE_HALF_STEP = false>
class StepperUnipolar {
 public:
  static constexpr uint16_t kStepsPerRevolution = STEPS_PER_REV;

  explicit constexpr StepperUnipolar(uint32_t step_delay_us = 2000)
      : step_delay_us_(step_delay_us) {}

  void Init() const {
    const gpio_num_t pin_list[] = {PH1, PH2, PH3, PH4};
    for (gpio_num_t pin : pin_list) {
      esp_rom_gpio_pad_select_gpio(pin);
      gpio_set_direction(pin, GPIO_MODE_OUTPUT);
      gpio_set_level(pin, 0);
    }
  }

  void Step(int32_t steps, float rpm = 0.0f) {
    if (steps == 0) {
      return;
    }
    if(rpm > 0.0f) {
      SetSpeedRpm(rpm);
    }

    int8_t direction = (steps > 0) ? 1 : -1;
    if constexpr (DIRECTION_INVERTED) {
      direction = static_cast<int8_t>(-direction);
    }

    const uint32_t count = (steps > 0)
                               ? static_cast<uint32_t>(steps)
                               : static_cast<uint32_t>(-static_cast<int64_t>(steps));

    const uint32_t effective_delay_us = EffectiveStepDelayUs();

    for (uint32_t i = 0; i < count; ++i) {
      uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
      while ((now_us - last_step_time_us_) < effective_delay_us) {
        now_us = static_cast<uint64_t>(esp_timer_get_time());
      }
      last_step_time_us_ = now_us;

      phase_ = WrapPhase(static_cast<int8_t>(phase_ + direction));
      ApplyPhase(phase_);
    }

    Release();
  }

  void Turn(float rotations, float rpm = 0.0f) {
    const float steps_f = rotations * static_cast<float>(STEPS_PER_REV);
    const int32_t steps =
        static_cast<int32_t>((steps_f >= 0.0f) ? (steps_f + 0.5f) : (steps_f - 0.5f));
    Step(steps, rpm);
  }

  void SetSpeedRpm(float rpm) {
    if (rpm == 0) {
      step_delay_us_ = 0;
      return;
    }
    step_delay_us_ = (60UL * 1000UL * 1000UL) / (static_cast<uint32_t>(STEPS_PER_REV) * static_cast<uint32_t>(rpm));
  }

  void SetStepDelayUs(uint32_t step_delay_us) { step_delay_us_ = step_delay_us; }

 private:
  static constexpr uint8_t kFullStepSequence[4][4] = {
      {1, 0, 1, 0},
      {0, 1, 1, 0},
      {0, 1, 0, 1},
      {1, 0, 0, 1},
  };

  static constexpr uint8_t kHalfStepSequence[8][4] = {
      {1, 0, 0, 0},
      {1, 0, 1, 0},
      {0, 0, 1, 0},
      {0, 1, 1, 0},
      {0, 1, 0, 0},
      {0, 1, 0, 1},
      {0, 0, 0, 1},
      {1, 0, 0, 1},
  };

  static constexpr int8_t kPhaseCount = USE_HALF_STEP ? 8 : 4;

  static constexpr uint8_t WrapPhase(int8_t phase) {
    return static_cast<uint8_t>((phase + kPhaseCount) % kPhaseCount);
  }

  static void ApplyPhase(uint8_t phase) {
    if constexpr (USE_HALF_STEP) {
      gpio_set_level(PH1, kHalfStepSequence[phase][0]);
      gpio_set_level(PH2, kHalfStepSequence[phase][1]);
      gpio_set_level(PH3, kHalfStepSequence[phase][2]);
      gpio_set_level(PH4, kHalfStepSequence[phase][3]);
    } else {
      gpio_set_level(PH1, kFullStepSequence[phase][0]);
      gpio_set_level(PH2, kFullStepSequence[phase][1]);
      gpio_set_level(PH3, kFullStepSequence[phase][2]);
      gpio_set_level(PH4, kFullStepSequence[phase][3]);
    }
  }

  static void Release() {
    gpio_set_level(PH1, 0);
    gpio_set_level(PH2, 0);
    gpio_set_level(PH3, 0);
    gpio_set_level(PH4, 0);
  }

  uint8_t phase_{0};
  uint32_t step_delay_us_;
  uint64_t last_step_time_us_{0};

  uint32_t EffectiveStepDelayUs() const {
    return (step_delay_us_ == 0) ? 1U : step_delay_us_;
  }
};
