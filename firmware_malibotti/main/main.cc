#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <array>
#include <sys/types.h>
#include <unistd.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rgbled.hh"
#include "esp_rom_gpio.h"

#include "hal/gpio_ll.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "buzzer.hh"
#include "listener/echo_message_processor.hh"
#include "listener/rgb_message_processor.hh"
#include "listener/servo_message_processor.hh"
#include "listener/stepper_message_processor.hh"
#include "stepper_unipolar.hh"
#include "usb.hh"

constexpr const char MONITOR_LOG_TAG[] = "monitor";

namespace P
{
  constexpr gpio_num_t BUTTON{GPIO_NUM_0};
  constexpr gpio_num_t LIMIT_RIGHT{GPIO_NUM_6};
  constexpr gpio_num_t LIMIT_LEFT{GPIO_NUM_7};
  constexpr gpio_num_t RGB_LED{GPIO_NUM_11};
  constexpr gpio_num_t STEPPER1_PH1{GPIO_NUM_21};
  constexpr gpio_num_t STEPPER1_PH2{GPIO_NUM_47};
  constexpr gpio_num_t STEPPER1_PH3{GPIO_NUM_34};
  constexpr gpio_num_t STEPPER1_PH4{GPIO_NUM_35};
  constexpr gpio_num_t STEPPER2_PH1{GPIO_NUM_36};
  constexpr gpio_num_t STEPPER2_PH2{GPIO_NUM_48};
  constexpr gpio_num_t STEPPER2_PH3{GPIO_NUM_33};
  constexpr gpio_num_t STEPPER2_PH4{GPIO_NUM_26};
  constexpr gpio_num_t SERVO{GPIO_NUM_37};
  constexpr gpio_num_t BUZZER{GPIO_NUM_38};
  constexpr gpio_num_t SDA{GPIO_NUM_39};
  constexpr gpio_num_t SCL{GPIO_NUM_40};
}

namespace stepper_settings
{
  constexpr uint16_t STEPS_PER_REV = 2048;
  constexpr uint32_t MAX_RPM = 12;
  constexpr TickType_t TURN_PAUSE = pdMS_TO_TICKS(500);
  constexpr bool USE_HALF_STEP = false;
}

constexpr TickType_t CDC_LOG_WRITE_TIMEOUT_TICKS = pdMS_TO_TICKS(20);

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
RGBLED::BlinkPattern NOT_MOUNTED(CRGB::Red, 250, CRGB::Black, 250);
RGBLED::BlinkPattern MOUNTED(CRGB::Green, 1000, CRGB::Black, 1000);
RGBLED::BlinkPattern SUSPENDED(CRGB::Blue, 250, CRGB::Black, 2250);
RGBLED::MultipleFlashesPattern USB_INIT_FAILED(CRGB::Red, 2);

RGBLED::M<1, RGBLED::DeviceType::WS2812> s_board_led;
BUZZER::M s_buzzer;
ISendBackInterface *s_sendBack;
int (*s_previous_log_vprintf)(const char *, va_list) = nullptr;

using Stepper1 = StepperUnipolar<P::STEPPER1_PH1,
                                 P::STEPPER1_PH3,
                                 P::STEPPER1_PH2,
                                 P::STEPPER1_PH4,
                                 stepper_settings::STEPS_PER_REV,
                                 false,
                                 stepper_settings::USE_HALF_STEP>;

using Stepper2 = StepperUnipolar<P::STEPPER2_PH1,
                                 P::STEPPER2_PH3,
                                 P::STEPPER2_PH2,
                                 P::STEPPER2_PH4,
                                 stepper_settings::STEPS_PER_REV,
                                 false,
                                 stepper_settings::USE_HALF_STEP>;

static Stepper1 s_stepper1;
static Stepper2 s_stepper2;

static std::array<IMessageProcessor *, 5> message_processors{
  new listener::EchoMessageProcessor(),
  new listener::RGBMessageProcessor(s_board_led),
  new listener::ServoMessageProcessor(P::SERVO),
  new listener::StepperMessageProcessor<0x0004>(s_stepper1),
  new listener::StepperMessageProcessor<0x0005>(s_stepper2)
};

static void process_vendor_binary_message(const uint8_t msg[usb_bridge::kBinaryMsgSize])
{
  uint16_t const name_space = static_cast<uint16_t>(msg[0] | (static_cast<uint16_t>(msg[1]) << 8));
  uint16_t const message_id = static_cast<uint16_t>(msg[2] | (static_cast<uint16_t>(msg[3]) << 8));
  uint8_t const *payload = msg + 4;

  IMessageProcessor *message_listener = nullptr;
  for (auto *processor : message_processors)
  {
    if (processor != nullptr && processor->GetNamespace() == name_space)
    {
      message_listener = processor;
      break;
    }
  }

  if (message_listener == nullptr)
  {
    return;
  }

  message_listener->Handle(*s_sendBack, message_id, payload);
}

static void processing_task(void *context)
{
  (void)context;

  while (true)
  {
    s_board_led.Refresh();
    s_buzzer.Loop();
    uint32_t const now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    for (auto *processor : message_processors)
    {
      if (processor != nullptr)
      {
        processor->Loop(*s_sendBack, now_ms);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

extern "C" void app_main(void)
{
  s_sendBack = new usb_bridge::VendorSendBack();
  usb_bridge::SetVendorMessageHandler(process_vendor_binary_message);

  s_stepper1.Init();
  s_stepper2.Init();

  s_board_led.Begin(SPI2_HOST, P::RGB_LED);
  s_buzzer.Begin(P::BUZZER);
  s_buzzer.PlaySong(1);

  for (auto *processor : message_processors)
  {
    if (processor != nullptr)
    {
      processor->Setup(*s_sendBack, 0);
    }
  }

  esp_rom_gpio_pad_select_gpio(P::BUTTON);
  gpio_set_direction(P::BUTTON, GPIO_MODE_INPUT);
  gpio_set_pull_mode(P::BUTTON, GPIO_PULLUP_ONLY);

  ESP_ERROR_CHECK(usb_bridge::Init());

  xTaskCreate(processing_task, "processing", 4096, nullptr, tskIDLE_PRIORITY + 2, nullptr);

  while (true)
  {
    ESP_LOGI(
        MONITOR_LOG_TAG,
        "mntd=%d sspnd=%d cdc_cnctd=%d cdc_rx=%u cdc_tx_free=%u btn=%d heap=%lu",
        tud_mounted() ? 1 : 0,
        tud_suspended() ? 1 : 0,
        tud_cdc_connected() ? 1 : 0,
        static_cast<unsigned>(tud_cdc_available()),
        static_cast<unsigned>(tud_cdc_write_available()),
        !gpio_get_level(P::BUTTON),
        static_cast<unsigned long>(esp_get_free_heap_size()));
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}
