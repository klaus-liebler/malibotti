#pragma once

#include <stdint.h>

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "listener/interfaces.hh"

namespace listener {
template <uint16_t kNamespaceStepper>
class StepperMessageProcessor : public IMessageProcessor {
public:
  static constexpr uint16_t kMessageEnqueueTurn = 0x0001;
  static constexpr uint16_t kMessageGetPendingCount = 0x0002;

  using StepperStepFunction = void (*)(void* stepper, int32_t steps, float rpm);

  template <typename TStepper>
  explicit StepperMessageProcessor(TStepper& stepper)
      : stepper_(static_cast<void*>(&stepper)),
        stepper_step_fn_(&StepAdapter<TStepper>) {}

  void Setup(ISendBackInterface& context, uint32_t now_ms) override;
  void Handle(ISendBackInterface& context, uint16_t message_id, const uint8_t* payload) override;
  uint16_t GetNamespace() const override;

private:
  struct StepperTurnJob {
    int32_t steps;
    uint16_t rpm_x10;
  };

  static constexpr uint8_t kQueueCapacity = 32;
  static constexpr uint16_t kDefaultRpmX10 = 120;

  void* stepper_;
  StepperStepFunction stepper_step_fn_;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t worker_task_handle_ = nullptr;
  std::atomic<bool> setup_done_{false};
  std::atomic<bool> job_active_{false};

  template <typename TStepper>
  static void StepAdapter(void* stepper, int32_t steps, float rpm) {
    static_cast<TStepper*>(stepper)->Step(steps, rpm);
  }

  static int32_t DecodeI32Le(const uint8_t* payload);
  static uint16_t DecodeU16Le(const uint8_t* payload);
  static void EncodeU16Le(uint8_t* payload, uint16_t value);
  static void WorkerTaskTrampoline(void* context);

  void WorkerTask();
  bool EnqueueTurn(const StepperTurnJob& job);
  uint16_t PendingCount() const;
  void HandleEnqueueTurn(uint16_t message_id, const uint8_t* payload);
  void HandleGetPendingCount(ISendBackInterface& context, uint16_t message_id);
};

}  // namespace listener
