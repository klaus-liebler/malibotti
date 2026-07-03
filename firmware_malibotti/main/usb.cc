#include "usb.hh"

constexpr size_t CONFIG_TOTAL_LEN{TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_VENDOR_DESC_LEN};

constexpr uint8_t EPNUM_CDC_NOTIF{0x81};
constexpr uint8_t EPNUM_CDC_OUT{0x02};
constexpr uint8_t EPNUM_CDC_IN{0x82};
constexpr uint8_t EPNUM_VENDOR_OUT{0x03};
constexpr uint8_t EPNUM_VENDOR_IN{0x83};

enum
{
  ITF_CDC = 0,
  ITF_CDC_DATA,
  ITF_VENDOR,
  ITF_TOTAL
};

namespace usb_bridge
{

  constinit char s_serial_number[2 * 6 + 1] = {};

  extern constexpr tusb_desc_device_t g_usb_device_descriptor =
      {
          .bLength = sizeof(tusb_desc_device_t),
          .bDescriptorType = TUSB_DESC_DEVICE,
          .bcdUSB = 0x0210,
          .bDeviceClass = TUSB_CLASS_MISC,
          .bDeviceSubClass = MISC_SUBCLASS_COMMON,
          .bDeviceProtocol = MISC_PROTOCOL_IAD,
          .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
          .idVendor = 0xCAFE,
          .idProduct = 0x4021,
          .bcdDevice = 0x0100,
          .iManufacturer = 0x01,
          .iProduct = 0x02,
          .iSerialNumber = 0x03,
          .bNumConfigurations = 0x01,
  };

  extern constexpr uint8_t g_usb_full_speed_configuration_descriptor[] =
      {
          TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
          TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
          TUD_VENDOR_DESCRIPTOR(ITF_VENDOR, 5, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64),
  };

  constexpr char kUsbLangId[] = {0x09, 0x04};
  constinit const char *g_usb_string_descriptor[] =
      {
          kUsbLangId,
          "Klaus Liebler, HS Osnabrueck",
          "Malibotti",
          s_serial_number,
          "Malibotti CDC",
          "Malibotti WebUSB",
  };

  static_assert(sizeof(g_usb_string_descriptor) / sizeof(g_usb_string_descriptor[0]) ==
                    g_usb_string_descriptor_count,
                "USB string descriptor count mismatch");

  static constinit VendorMessageHandler s_vendor_message_handler = nullptr;

  void SetVendorMessageHandler(VendorMessageHandler handler)
  {
    s_vendor_message_handler = handler;
  }

  bool VendorSendBack::Send(uint16_t name_space, uint16_t message_id, const uint8_t *payload)
  {
    if (payload == nullptr)
    {
      return false;
    }

    uint8_t frame[kBinaryMsgSize] = {};
    frame[0] = static_cast<uint8_t>(name_space & 0xFF);
    frame[1] = static_cast<uint8_t>((name_space >> 8) & 0xFF);
    frame[2] = static_cast<uint8_t>(message_id & 0xFF);
    frame[3] = static_cast<uint8_t>((message_id >> 8) & 0xFF);
    memcpy(frame + 4, payload, kBinaryPayloadSize);

    if (tud_vendor_write_available() < kBinaryMsgSize)
    {
      return false;
    }

    uint32_t const written = tud_vendor_write(frame, kBinaryMsgSize);
    tud_vendor_write_flush();
    return written == kBinaryMsgSize;
  }

  void device_event_handler(tinyusb_event_t *event, void *arg)
  {
    (void)arg;
    switch (event->id)
    {
    case TINYUSB_EVENT_ATTACHED:
    case TINYUSB_EVENT_DETACHED:
    default:
      break;
    }
  }

  void cdc_rx_callback(int itf_as_int, cdcacm_event_t *event)
  {
    (void)event;
    tinyusb_cdcacm_itf_t itf = static_cast<tinyusb_cdcacm_itf_t>(itf_as_int);
    uint8_t rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];
    size_t rx_size = 0;
    ESP_ERROR_CHECK(tinyusb_cdcacm_read(itf, rx_buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size));
  }

  esp_err_t Init()
  {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_serial_number, sizeof(s_serial_number), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG(device_event_handler);
    tusb_cfg.descriptor.device = &g_usb_device_descriptor;
    tusb_cfg.descriptor.string = g_usb_string_descriptor;
    tusb_cfg.descriptor.string_count = g_usb_string_descriptor_count;
    tusb_cfg.descriptor.full_speed_config = g_usb_full_speed_configuration_descriptor;
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = &cdc_rx_callback,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
    ESP_ERROR_CHECK(tinyusb_console_init(TINYUSB_CDC_ACM_0));

    return ESP_OK;
  }

  //--------------------------------------------------------------------+
  // BOS Descriptor
  //--------------------------------------------------------------------+

  /* Microsoft OS 2.0 registry property descriptor
  Per MS requirements https://msdn.microsoft.com/en-us/library/windows/hardware/hh450799(v=vs.85).aspx
  device should create DeviceInterfaceGUIDs. It can be done by driver and
  in case of real PnP solution device should expose MS "Microsoft OS 2.0
  registry property descriptor". Such descriptor can insert any record
  into Windows registry per device/configuration/interface. In our case it
  will insert "DeviceInterfaceGUIDs" multistring property.

  GUID is freshly generated and should be OK to use.

  https://developers.google.com/web/fundamentals/native-hardware/build-for-webusb/
  (Section Microsoft OS compatibility descriptors)
  */

  constexpr size_t BOS_TOTAL_LEN{TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN};

  constexpr uint16_t MS_OS_20_DESC_LEN{0xB2};

  // BOS Descriptor is required for webUSB
  constexpr uint8_t const desc_bos[] =
      {
          // total length, number of device caps
          TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),

          // Vendor Code, iLandingPage
          TUD_BOS_WEBUSB_DESCRIPTOR(kVendorRequestWebUsb, 1),

          // Microsoft OS 2.0 descriptor
          TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, kVendorRequestMicrosoft)};

  extern "C" uint8_t const *tud_descriptor_bos_cb(void)
  {
    return usb_bridge::desc_bos;
  }

  extern constexpr uint8_t desc_ms_os_20[] =
      {
          // Set header: length, type, windows version, total length
          U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

          // Configuration subset header: length, type, configuration index, reserved, configuration total length
          U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

          // Function Subset header: length, type, first interface, reserved, subset length
          U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_VENDOR, 0, U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

          // MS OS 2.0 Compatible ID descriptor: length, type, compatible ID, sub compatible ID
          U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID), 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // sub-compatible

          // MS OS 2.0 Registry property descriptor: length, type
          U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
          U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A), // wPropertyDataType, wPropertyNameLength and PropertyName "DeviceInterfaceGUIDs\0" in UTF-16
          'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
          'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00, 0x00, 0x00,
          U16_TO_U8S_LE(0x0050), // wPropertyDataLength
                                 // bPropertyData: “{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}”.
          '{', 0x00, '9', 0x00, '7', 0x00, '5', 0x00, 'F', 0x00, '4', 0x00, '4', 0x00, 'D', 0x00, '9', 0x00, '-', 0x00,
          '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
          '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
          '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00};

  TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

} // namespace usb_bridge

extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                           tusb_control_request_t const *request)
{
  if (stage != CONTROL_STAGE_SETUP)
  {
    return true;
  }

  switch (request->bmRequestType_bit.type)
  {
  case TUSB_REQ_TYPE_VENDOR:
    switch (request->bRequest)
    {
    case usb_bridge::kVendorRequestWebUsb:
      return tud_control_xfer(rhport, request, (void *)(uintptr_t)&usb_bridge::kDescUrl,
                              usb_bridge::kDescUrl.bLength);

    case usb_bridge::kVendorRequestMicrosoft:
      if (request->wIndex == 7)
      {
        uint16_t total_len;
        memcpy(&total_len, usb_bridge::desc_ms_os_20 + 8, 2);
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)usb_bridge::desc_ms_os_20,
                                total_len);
      }
      return false;

    default:
      break;
    }
    break;

  default:
    break;
  }

  return false;
}

extern "C" void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint16_t bufsize)
{
  (void)idx;
  (void)buffer;
  (void)bufsize;

  while (tud_vendor_available() >= usb_bridge::kBinaryMsgSize)
  {
    uint8_t msg[usb_bridge::kBinaryMsgSize];
    uint32_t const count = tud_vendor_read(msg, usb_bridge::kBinaryMsgSize);
    if (count == usb_bridge::kBinaryMsgSize && usb_bridge::s_vendor_message_handler != nullptr)
    {
      usb_bridge::s_vendor_message_handler(msg);
    }
  }
}
