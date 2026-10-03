#include "tusb.h"

enum {
    ITF_NUM_MIDI = 0,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL,
};

#define EPNUM_MIDI 0x01u
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE,
    .idProduct = 0x4010,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI, 0, EPNUM_MIDI, 0x80u | EPNUM_MIDI, 64),
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

static const char *const string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Clavicore",
    "Clavicore Keyboard",
    "Clavicore-001",
};

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
    static uint16_t descriptor[32];
    uint8_t character_count;

    (void)language_id;
    if (index == 0u) {
        descriptor[1] = (uint16_t)((uint8_t)string_descriptors[0][0] |
                                   ((uint8_t)string_descriptors[0][1] << 8u));
        character_count = 1u;
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
            return NULL;
        }
        const char *string = string_descriptors[index];
        character_count = 0u;
        while (string[character_count] != '\0' && character_count < 31u) {
            descriptor[character_count + 1u] = (uint8_t)string[character_count];
            ++character_count;
        }
    }

    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8u) | (2u * character_count + 2u));
    return descriptor;
}