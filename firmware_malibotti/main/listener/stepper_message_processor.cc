#include "listener/stepper_message_processor.hh"

#include "esp_log.h"

namespace listener {

namespace {

constexpr char kStepperLogTag[] = "StepperMessageProcessor";

}  // namespace

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::Setup(ISendBackInterface& context, uint32_t now_ms) {
  (void)context;
  (void)now_ms;

  if (setup_done_.load()) {
    return;
  }

  queue_ = xQueueCreate(kQueueCapacity, sizeof(StepperTurnJob));
  if (queue_ == nullptr) {
    ESP_LOGE(kStepperLogTag, "Failed to create stepper queue");
    return;
  }

  BaseType_t const created = xTaskCreate(
      &StepperMessageProcessor::WorkerTaskTrampoline,
      "stepper_worker",
      4096,
      this,
      tskIDLE_PRIORITY + 2,
      &worker_task_handle_);
  if (created != pdPASS) {
    ESP_LOGE(kStepperLogTag, "Failed to create stepper worker task");
    vQueueDelete(queue_);
    queue_ = nullptr;
    return;
  }

  setup_done_.store(true);
}

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::Handle(ISendBackInterface& context, uint16_t message_id, const uint8_t* payload) {
  if (payload == nullptr) {
    return;
  }

  switch (message_id) {
    case kMessageEnqueueTurn:
      HandleEnqueueTurn(message_id, payload);
      break;
    case kMessageGetPendingCount:
      HandleGetPendingCount(context, message_id);
      break;
    default:
      break;
  }
}

template <uint16_t kNamespaceStepper>
uint16_t StepperMessageProcessor<kNamespaceStepper>::GetNamespace() const {
  return kNamespaceStepper;
}

template <uint16_t kNamespaceStepper>
int32_t StepperMessageProcessor<kNamespaceStepper>::DecodeI32Le(const uint8_t* payload) {
  return static_cast<int32_t>(
      static_cast<uint32_t>(payload[0]) |
      (static_cast<uint32_t>(payload[1]) << 8) |
      (static_cast<uint32_t>(payload[2]) << 16) |
      (static_cast<uint32_t>(payload[3]) << 24));
}

template <uint16_t kNamespaceStepper>
uint16_t StepperMessageProcessor<kNamespaceStepper>::DecodeU16Le(const uint8_t* payload) {
  return static_cast<uint16_t>(
      static_cast<uint16_t>(payload[0]) |
      (static_cast<uint16_t>(payload[1]) << 8));
}

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::EncodeU16Le(uint8_t* payload, uint16_t value) {
  payload[0] = static_cast<uint8_t>(value & 0xFF);
  payload[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::WorkerTaskTrampoline(void* context) {
  auto* self = static_cast<StepperMessageProcessor<kNamespaceStepper>*>(context);
  self->WorkerTask();
}

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::WorkerTask() {
  while (true) {
    StepperTurnJob job{};
    if (xQueueReceive(queue_, &job, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    job_active_.store(true);

    float const rpm = static_cast<float>((job.rpm_x10 == 0) ? kDefaultRpmX10 : job.rpm_x10) / 10.0f;
    stepper_step_fn_(stepper_, job.steps, rpm);

    job_active_.store(false);
  }
}

template <uint16_t kNamespaceStepper>
bool StepperMessageProcessor<kNamespaceStepper>::EnqueueTurn(const StepperTurnJob& job) {
  if (queue_ == nullptr) {
    return false;
  }
  return xQueueSend(queue_, &job, 0) == pdTRUE;
}

template <uint16_t kNamespaceStepper>
uint16_t StepperMessageProcessor<kNamespaceStepper>::PendingCount() const {
  uint16_t pending = 0;
  if (queue_ != nullptr) {
    pending = static_cast<uint16_t>(uxQueueMessagesWaiting(queue_));
  }
  if (job_active_.load()) {
    ++pending;
  }
  return pending;
}

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::HandleEnqueueTurn(uint16_t message_id, const uint8_t* payload) {
  (void)message_id;

  StepperTurnJob const job{
      DecodeI32Le(payload),
      DecodeU16Le(payload + 4),
  };

  if (job.steps == 0) {
    return;
  }

  if (!EnqueueTurn(job)) {
    ESP_LOGW(kStepperLogTag, "Stepper queue full. Dropping job steps=%ld", static_cast<long>(job.steps));
  }
}

template <uint16_t kNamespaceStepper>
void StepperMessageProcessor<kNamespaceStepper>::HandleGetPendingCount(ISendBackInterface& context, uint16_t message_id) {
  uint8_t response[60] = {};
  EncodeU16Le(response, PendingCount());
  (void)context.Send(kNamespaceStepper, message_id, response);
}

template class StepperMessageProcessor<0x0004>;
template class StepperMessageProcessor<0x0005>;

}  // namespace listener
