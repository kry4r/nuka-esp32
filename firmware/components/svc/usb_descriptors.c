// SPDX-License-Identifier: AGPL-3.0-or-later
#include "usb_descriptors.h"
#include "esp_mac.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

enum { ITF_CDC, ITF_CDC_DATA, ITF_KEYBOARD, ITF_CONSUMER, ITF_COUNT };
static const uint8_t keyboard[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
static const uint8_t consumer[] = { TUD_HID_REPORT_DESC_CONSUMER() };
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0x303a,
    .idProduct = 0x4001, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0,
        TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + 2 * TUD_HID_DESC_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
    TUD_HID_DESCRIPTOR(ITF_KEYBOARD, 5, HID_ITF_PROTOCOL_KEYBOARD,
        sizeof(keyboard), 0x83, 8, 5),
    TUD_HID_DESCRIPTOR(ITF_CONSUMER, 6, HID_ITF_PROTOCOL_NONE,
        sizeof(consumer), 0x84, 2, 5)
};
static char serial[13];
static const char *strings[] = { "\x09\x04", "nuka", "nuka-esp32", serial,
    "nuka CDC", "nuka keyboard", "nuka media" };

void usb_descriptors(tinyusb_desc_config_t *descriptors)
{
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    *descriptors = (tinyusb_desc_config_t){ .device = &device,
        .string = strings, .string_count = sizeof(strings) / sizeof(strings[0]),
        .full_speed_config = configuration };
}
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{ return instance == 0 ? keyboard : consumer; }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t *buffer, uint16_t requested)
{
    size_t size = instance == 0 ? 8 : 2;
    if (requested < size) size = requested;
    memset(buffer, 0, size); return size;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t const *buffer, uint16_t size)
{
    // Host keyboard LED reports have no on-device indicator.
}
