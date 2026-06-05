#include "mitsubishi_v0.h"
#include "protocols_common.h"
#include <string.h>

// Original implementation by @lupettohf

#define MITSUBISHI_BIT_COUNT  96
#define MITSUBISHI_DATA_BYTES 12

static const SubGhzBlockConst subghz_protocol_mitsubishi_const = {
    .te_short = 250,
    .te_long = 500,
    .te_delta = 100,
    .min_count_bit_for_found = 80,
};

typedef enum {
    MitsubishiDecoderStepReset = 0,
    MitsubishiDecoderStepDataSave,
    MitsubishiDecoderStepDataCheck,
} MitsubishiDecoderStep;

struct SubGhzProtocolDecoderMitsubishi {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    uint8_t decoder_state;
    uint16_t bit_count;
    uint8_t decode_data[MITSUBISHI_DATA_BYTES];
};

static void mitsubishi_unscramble_payload(uint8_t* payload) {
    for(uint8_t i = 0; i < 8; i++) {
        payload[i] = (uint8_t)~payload[i];
    }

    uint16_t counter = ((uint16_t)payload[4] << 8) | payload[5];
    uint8_t hi = (counter >> 8) & 0xFF;
    uint8_t lo = counter & 0xFF;
    uint8_t mask1 = (hi & 0xAAU) | (lo & 0x55U);
    uint8_t mask2 = (lo & 0xAAU) | (hi & 0x55U);
    uint8_t mask3 = mask1 ^ mask2;

    for(uint8_t i = 0; i < 5; i++) {
        payload[i] ^= mask3;
    }
}

static void mitsubishi_reset_payload(SubGhzProtocolDecoderMitsubishi* instance) {
    instance->bit_count = 0;
    memset(instance->decode_data, 0, sizeof(instance->decode_data));
}

static bool mitsubishi_collect_pair(
    SubGhzProtocolDecoderMitsubishi* instance,
    uint32_t high,
    uint32_t low) {
    bool bit_value;

    if(pp_is_short(high, &subghz_protocol_mitsubishi_const) &&
       pp_is_long(low, &subghz_protocol_mitsubishi_const)) {
        bit_value = true;
    } else if(
        pp_is_long(high, &subghz_protocol_mitsubishi_const) &&
        pp_is_short(low, &subghz_protocol_mitsubishi_const)) {
        bit_value = false;
    } else {
        return false;
    }

    uint16_t bit_index = instance->bit_count;
    if(bit_index < MITSUBISHI_BIT_COUNT) {
        if(bit_value) {
            uint8_t byte_index = bit_index >> 3;
            uint8_t bit_position = 7 - (bit_index & 0x07);
            instance->decode_data[byte_index] |= (1U << bit_position);
        }
        instance->bit_count++;
    }

    return true;
}

static void mitsubishi_publish_frame(SubGhzProtocolDecoderMitsubishi* instance) {
    uint8_t payload[MITSUBISHI_DATA_BYTES];
    memcpy(payload, instance->decode_data, sizeof(payload));
    mitsubishi_unscramble_payload(payload);

    instance->generic.data_count_bit = instance->bit_count;
    instance->generic.serial = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                               ((uint32_t)payload[2] << 8) | payload[3];
    instance->generic.cnt = ((uint16_t)payload[4] << 8) | payload[5];
    instance->generic.btn = payload[6];

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

const SubGhzProtocolDecoder subghz_protocol_mitsubishi_decoder = {
    .alloc = subghz_protocol_decoder_mitsubishi_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_mitsubishi_feed,
    .reset = subghz_protocol_decoder_mitsubishi_reset,
    .get_hash_data = subghz_protocol_decoder_mitsubishi_get_hash_data,
    .serialize = subghz_protocol_decoder_mitsubishi_serialize,
    .deserialize = subghz_protocol_decoder_mitsubishi_deserialize,
    .get_string = subghz_protocol_decoder_mitsubishi_get_string,
};

#ifdef ENABLE_EMULATE_FEATURE
// =========================================================================
// ENCODER
// =========================================================================

#define MITS_V0_BURSTS         6U
#define MITS_V0_GAP_US         18000U
#define MITS_V0_UPLOAD_CAPACITY (MITSUBISHI_BIT_COUNT * 2U * MITS_V0_BURSTS + 10U)
_Static_assert(
    MITS_V0_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "MITS_V0_UPLOAD_CAPACITY exceeds shared upload slab");

typedef struct SubGhzProtocolEncoderMitsubishi {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint32_t serial;
    uint8_t btn;
    uint16_t cnt;
} SubGhzProtocolEncoderMitsubishi;

static void subghz_protocol_encoder_mitsubishi_get_upload(
    SubGhzProtocolEncoderMitsubishi* instance) {
    furi_check(instance);

    // Reconstruct raw bytes from serial/cnt/btn.
    // Derivation: decoded[i] = ~raw[i] ^ (raw[4] ^ raw[5])  for i=0..3
    //             decoded[5] = ~raw[5]  (byte 4 after unscramble also equals ~raw[5])
    // Choosing raw[4]=0: mask3 = raw[5] = ~cnt_lo  → raw[i] = serial_byte[i] ^ cnt_lo
    uint8_t cnt_lo = (uint8_t)(instance->cnt & 0xFF);
    uint8_t raw[MITSUBISHI_DATA_BYTES] = {0};
    raw[0] = (uint8_t)((instance->serial >> 24) & 0xFF) ^ cnt_lo;
    raw[1] = (uint8_t)((instance->serial >> 16) & 0xFF) ^ cnt_lo;
    raw[2] = (uint8_t)((instance->serial >> 8) & 0xFF) ^ cnt_lo;
    raw[3] = (uint8_t)(instance->serial & 0xFF) ^ cnt_lo;
    raw[4] = 0;
    raw[5] = (uint8_t)~cnt_lo;
    raw[6] = (uint8_t)~instance->btn;

    uint32_t te_short = subghz_protocol_mitsubishi_const.te_short;
    uint32_t te_long = subghz_protocol_mitsubishi_const.te_long;
    size_t index = 0;

#define ADD_LEVEL(lvl, dur) \
    index = pp_emit_merge(instance->encoder.upload, index, MITS_V0_UPLOAD_CAPACITY, (lvl), (dur))

    for(uint8_t burst = 0; burst < MITS_V0_BURSTS; burst++) {
        for(uint8_t byte_i = 0; byte_i < MITSUBISHI_DATA_BYTES; byte_i++) {
            for(int8_t bit_i = 7; bit_i >= 0; bit_i--) {
                bool bit = (raw[byte_i] >> bit_i) & 1U;
                if(bit) {
                    ADD_LEVEL(true, te_short);
                    ADD_LEVEL(false, te_long);
                } else {
                    ADD_LEVEL(true, te_long);
                    ADD_LEVEL(false, te_short);
                }
            }
        }
        if(burst < MITS_V0_BURSTS - 1U) {
            ADD_LEVEL(false, MITS_V0_GAP_US);
        }
    }

#undef ADD_LEVEL

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

void* subghz_protocol_encoder_mitsubishi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderMitsubishi* instance =
        malloc(sizeof(SubGhzProtocolEncoderMitsubishi));
    furi_check(instance);
    instance->base.protocol = &mitsubishi_v0_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1;
    pp_encoder_buffer_ensure(instance, MITS_V0_UPLOAD_CAPACITY);
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    return instance;
}

SubGhzProtocolStatus subghz_protocol_encoder_mitsubishi_deserialize(
    void* context,
    FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderMitsubishi* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        if(pp_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
           SubGhzProtocolStatusOk) {
            break;
        }

        uint32_t serial = UINT32_MAX, btn = UINT32_MAX, cnt = UINT32_MAX;
        pp_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);
        if(serial == UINT32_MAX || btn == UINT32_MAX || cnt == UINT32_MAX) break;

        instance->serial = serial;
        instance->btn = (uint8_t)btn;
        instance->cnt = (uint16_t)cnt;
        instance->generic.serial = serial;
        instance->generic.btn = (uint8_t)btn;
        instance->generic.cnt = cnt;

        instance->encoder.repeat = pp_encoder_read_repeat(flipper_format, 1);

        subghz_protocol_encoder_mitsubishi_get_upload(instance);

        if(instance->encoder.size_upload == 0) break;

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

const SubGhzProtocolEncoder subghz_protocol_mitsubishi_encoder = {
    .alloc = subghz_protocol_encoder_mitsubishi_alloc,
    .free = pp_encoder_free,
    .deserialize = subghz_protocol_encoder_mitsubishi_deserialize,
    .stop = pp_encoder_stop,
    .yield = pp_encoder_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_mitsubishi_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol mitsubishi_v0_protocol = {
    .name = MITSUBISHI_PROTOCOL_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save
#ifdef ENABLE_EMULATE_FEATURE
            | SubGhzProtocolFlag_Send
#endif
    ,
    .decoder = &subghz_protocol_mitsubishi_decoder,
    .encoder = &subghz_protocol_mitsubishi_encoder,
};

void* subghz_protocol_decoder_mitsubishi_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMitsubishi* instance = calloc(1, sizeof(SubGhzProtocolDecoderMitsubishi));
    furi_check(instance);
    instance->base.protocol = &mitsubishi_v0_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_mitsubishi_reset(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    instance->decoder_state = MitsubishiDecoderStepReset;
    instance->decoder.te_last = 0;
    instance->generic.data_count_bit = 0;
    mitsubishi_reset_payload(instance);
}

void subghz_protocol_decoder_mitsubishi_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;

    switch(instance->decoder_state) {
    case MitsubishiDecoderStepReset:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder_state = MitsubishiDecoderStepDataCheck;
        }
        break;

    case MitsubishiDecoderStepDataSave:
        if(level) {
            instance->decoder.te_last = duration;
            instance->decoder_state = MitsubishiDecoderStepDataCheck;
        } else {
            instance->decoder_state = MitsubishiDecoderStepReset;
            mitsubishi_reset_payload(instance);
        }
        break;

    case MitsubishiDecoderStepDataCheck:
        if(!level) {
            if(mitsubishi_collect_pair(instance, instance->decoder.te_last, duration)) {
                if(instance->bit_count >= MITSUBISHI_BIT_COUNT) {
                    mitsubishi_publish_frame(instance);
                    mitsubishi_reset_payload(instance);
                    instance->decoder_state = MitsubishiDecoderStepReset;
                } else {
                    instance->decoder_state = MitsubishiDecoderStepDataSave;
                }
            } else {
                mitsubishi_reset_payload(instance);
                instance->decoder_state = MitsubishiDecoderStepReset;
            }
        } else {
            instance->decoder.te_last = duration;
        }
        break;
    }
}

uint8_t subghz_protocol_decoder_mitsubishi_get_hash_data(void* context) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    uint8_t hash = 0;
    for(size_t i = 0; i < sizeof(instance->decode_data); i++) {
        hash ^= instance->decode_data[i];
    }
    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_mitsubishi_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(ret == SubGhzProtocolStatusOk) {
        pp_serialize_fields(
            flipper_format,
            PP_FIELD_SERIAL | PP_FIELD_BTN | PP_FIELD_CNT,
            instance->generic.serial,
            instance->generic.btn,
            instance->generic.cnt,
            0);
    }
    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_mitsubishi_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;
    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_mitsubishi_const.min_count_bit_for_found);

    if(ret == SubGhzProtocolStatusOk) {
        uint32_t btn = instance->generic.btn;
        pp_encoder_read_fields(
            flipper_format, &instance->generic.serial, &btn, &instance->generic.cnt, NULL);
        instance->generic.btn = (uint8_t)btn;
    }

    return ret;
}

void subghz_protocol_decoder_mitsubishi_get_string(void* context, FuriString* output) {
    furi_check(context);
    SubGhzProtocolDecoderMitsubishi* instance = context;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Sn:%08lX Cnt:%04lX\r\n"
        "Btn:%02X\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        instance->generic.serial,
        instance->generic.cnt,
        instance->generic.btn);
}
