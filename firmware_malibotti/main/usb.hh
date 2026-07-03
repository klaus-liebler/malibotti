#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_mac.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"

#include "listener/interfaces.hh"

namespace usb_bridge
{

    constexpr const char kUrl[] = "liebler.iui.hs-osnabrueck.de/malibotti/";
    constexpr uint32_t kBinaryMsgSize = 64;
    constexpr uint16_t kBinaryPayloadSize = 60;
    constexpr uint8_t kVendorRequestWebUsb = 1;
    constexpr uint8_t kVendorRequestMicrosoft = 2;

    extern char s_serial_number[2 * 6 + 1];
    extern const tusb_desc_device_t g_usb_device_descriptor;
    extern const uint8_t g_usb_full_speed_configuration_descriptor[];
    extern const char *g_usb_string_descriptor[];
    inline constexpr int g_usb_string_descriptor_count = 6;
    extern const uint8_t desc_ms_os_20[];

    using VendorMessageHandler = void (*)(const uint8_t msg[kBinaryMsgSize]);

    void SetVendorMessageHandler(VendorMessageHandler handler);

    class VendorSendBack final : public ISendBackInterface
    {
    public:
        bool Send(uint16_t name_space, uint16_t message_id, const uint8_t *payload) override;
    };

    struct webusb_url_desc_t
    {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint8_t bScheme;
        char url[sizeof(kUrl)];
    };

    constexpr webusb_url_desc_t make_desc_url()
    {
        webusb_url_desc_t d{
            static_cast<uint8_t>(3 + sizeof(kUrl) - 1),
            3, // WEBUSB URL type
            1, // 0: http, 1: https
            {},
        };
        for (size_t i = 0; i < sizeof(kUrl); ++i)
        {
            d.url[i] = kUrl[i];
        }
        return d;
    }

    inline constexpr webusb_url_desc_t kDescUrl = make_desc_url();

    void device_event_handler(tinyusb_event_t *event, void *arg);

    void cdc_rx_callback(int itf_as_int, cdcacm_event_t *event);

    esp_err_t Init();

} // namespace usb_bridge

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                           tusb_control_request_t const *request);

extern "C" void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint16_t bufsize);
