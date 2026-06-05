#include "ford_v3.h"
#include "../protopirate_app_i.h"
#include "protocols_common.h"
#include <string.h>

#define FORD_V3_TE_SHORT     240U
#define FORD_V3_TE_LONG      480U
#define FORD_V3_TE_DELTA     60U
#define FORD_V3_DATA_BITS    104U
#define FORD_V3_DATA_BYTES   13U
#define FORD_V3_PREAMBLE_MIN 30U

#define FORD_V3_BTN_LOCK   0x01U
#define FORD_V3_BTN_UNLOCK 0x02U

static const SubGhzBlockConst subghz_protocol_ford_v3_const = {
    .te_short = FORD_V3_TE_SHORT,
    .te_long = FORD_V3_TE_LONG,
    .te_delta = FORD_V3_TE_DELTA,
    .min_count_bit_for_found = FORD_V3_DATA_BITS,
};

typedef enum {
    FordV3DecoderStepReset = 0,
    FordV3DecoderStepPreamble = 1,
    FordV3DecoderStepData = 2,
} FordV3DecoderStep;

typedef struct SubGhzProtocolDecoderFordV3 {
    SubGhzProtocolDecoderBase base;
    SubGhzBlockDecoder decoder;
    SubGhzBlockGeneric generic;

    ManchesterState manchester_state;
    uint8_t raw_bytes[FORD_V3_DATA_BYTES];
    uint8_t bit_count;
    uint16_t preamble_count;

    uint32_t serial;
    uint16_t counter;
} SubGhzProtocolDecoderFordV3;

static void ford_v3_reset_data(SubGhzProtocolDecoderFordV3* instance);
static void ford_v3_add_bit(SubGhzProtocolDecoderFordV3* instance, bool bit);
static void ford_v3_parse_fields(SubGhzProtocolDecoderFordV3* instance);
static void ford_v3_emit_if_ready(SubGhzProtocolDecoderFordV3* instance);
static const char* ford_v3_button_name(uint8_t btn);

static const char* ford_v3_button_name(uint8_t btn) {
    switch(btn) {
    case FORD_V3_BTN_LOCK:
        return "Lock";
    case FORD_V3_BTN_UNLOCK:
        return "Unlock";
    default:
        return "?";
    }
}

static void ford_v3_reset_data(SubGhzProtocolDecoderFordV3* instance) {
    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));
    instance->bit_count = 0;
    instance->preamble_count = 0;
    manchester_advance(
        instance->manchester_state, ManchesterEventReset, &instance->manchester_state, NULL);
}

static void ford_v3_add_bit(SubGhzProtocolDecoderFordV3* instance, bool bit) {
    if(instance->bit_count >= FORD_V3_DATA_BITS) {
        return;
    }

    const uint8_t byte_index = instance->bit_count / 8U;
    const uint8_t bit_in_byte = 7U - (instance->bit_count % 8U);
    if(bit) {
        instance->raw_bytes[byte_index] |= (uint8_t)(1U << bit_in_byte);
    }
    instance->bit_count++;
}

static void ford_v3_parse_fields(SubGhzProtocolDecoderFordV3* instance) {
    const uint8_t* b = instance->raw_bytes;

    instance->serial = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 8) |
                       (uint32_t)b[4];
    instance->counter =
        (uint16_t)((((uint16_t)(uint8_t)~b[7]) << 8) | (uint8_t)~b[8]);

    instance->generic.serial = instance->serial;
    instance->generic.btn = (b[6] & 0x01U) ? FORD_V3_BTN_UNLOCK : FORD_V3_BTN_LOCK;
    instance->generic.cnt = instance->counter;
}

static void ford_v3_emit_if_ready(SubGhzProtocolDecoderFordV3* instance) {
    if(instance->bit_count < FORD_V3_DATA_BITS) {
        return;
    }

    instance->generic.data_count_bit = FORD_V3_DATA_BITS;
    ford_v3_parse_fields(instance);

    if(instance->base.callback) {
        instance->base.callback(&instance->base, instance->base.context);
    }
}

void* subghz_protocol_decoder_ford_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);

    SubGhzProtocolDecoderFordV3* instance = calloc(1, sizeof(SubGhzProtocolDecoderFordV3));
    furi_check(instance);

    instance->base.protocol = &ford_protocol_v3;
    instance->generic.protocol_name = instance->base.protocol->name;

    return instance;
}

void subghz_protocol_decoder_ford_v3_reset(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderFordV3* instance = context;
    instance->decoder.parser_step = FordV3DecoderStepReset;
    ford_v3_reset_data(instance);
}

void subghz_protocol_decoder_ford_v3_feed(void* context, bool level, uint32_t duration) {
    furi_check(context);

    SubGhzProtocolDecoderFordV3* instance = context;

    switch(instance->decoder.parser_step) {
    case FordV3DecoderStepReset:
        if(pp_is_short(duration, &subghz_protocol_ford_v3_const)) {
            ford_v3_reset_data(instance);
            instance->preamble_count = 1U;
            instance->decoder.parser_step = FordV3DecoderStepPreamble;
        }
        break;

    case FordV3DecoderStepPreamble:
        if(pp_is_short(duration, &subghz_protocol_ford_v3_const)) {
            instance->preamble_count++;
        } else if(
            instance->preamble_count >= FORD_V3_PREAMBLE_MIN &&
            pp_is_long(duration, &subghz_protocol_ford_v3_const)) {
            instance->manchester_state = ManchesterStateMid1;

            const ManchesterEvent event =
                level ? ManchesterEventLongHigh : ManchesterEventLongLow;

            bool data_bit = false;
            const bool valid = manchester_advance(
                instance->manchester_state, event, &instance->manchester_state, &data_bit);
            if(valid) {
                ford_v3_add_bit(instance, data_bit);
            }
            instance->decoder.parser_step = FordV3DecoderStepData;
        } else {
            instance->decoder.parser_step = FordV3DecoderStepReset;
        }
        break;

    case FordV3DecoderStepData: {
        if(!pp_is_short(duration, &subghz_protocol_ford_v3_const) &&
           !pp_is_long(duration, &subghz_protocol_ford_v3_const)) {
            ford_v3_emit_if_ready(instance);
            instance->decoder.parser_step = FordV3DecoderStepReset;

            if(pp_is_short(duration, &subghz_protocol_ford_v3_const)) {
                ford_v3_reset_data(instance);
                instance->preamble_count = 1U;
                instance->decoder.parser_step = FordV3DecoderStepPreamble;
            }
            break;
        }

        ManchesterEvent event;
        if(level) {
            event = pp_is_short(duration, &subghz_protocol_ford_v3_const) ? ManchesterEventShortHigh :
                                                                           ManchesterEventLongHigh;
        } else {
            event = pp_is_short(duration, &subghz_protocol_ford_v3_const) ? ManchesterEventShortLow :
                                                                           ManchesterEventLongLow;
        }

        bool data_bit = false;
        const bool valid = manchester_advance(
            instance->manchester_state, event, &instance->manchester_state, &data_bit);

        if(valid) {
            ford_v3_add_bit(instance, data_bit);
            if(instance->bit_count >= FORD_V3_DATA_BITS) {
                ford_v3_emit_if_ready(instance);
                instance->decoder.parser_step = FordV3DecoderStepReset;
            }
        }
        break;
    }
    }
}

uint8_t subghz_protocol_decoder_ford_v3_get_hash_data(void* context) {
    furi_check(context);

    SubGhzProtocolDecoderFordV3* instance = context;
    uint8_t hash = 0;

    for(size_t i = 0; i < FORD_V3_DATA_BYTES; i++) {
        hash ^= instance->raw_bytes[i];
    }

    return hash;
}

SubGhzProtocolStatus subghz_protocol_decoder_ford_v3_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_check(context);

    SubGhzProtocolDecoderFordV3* instance = context;

    instance->generic.data = ((uint64_t)instance->raw_bytes[0] << 56) |
                             ((uint64_t)instance->raw_bytes[1] << 48) |
                             ((uint64_t)instance->raw_bytes[2] << 40) |
                             ((uint64_t)instance->raw_bytes[3] << 32) |
                             ((uint64_t)instance->raw_bytes[4] << 24) |
                             ((uint64_t)instance->raw_bytes[5] << 16) |
                             ((uint64_t)instance->raw_bytes[6] << 8) |
                             (uint64_t)instance->raw_bytes[7];
    instance->generic.data_count_bit = FORD_V3_DATA_BITS;

    SubGhzProtocolStatus ret =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);

    if(ret == SubGhzProtocolStatusOk) {
        flipper_format_rewind(flipper_format);
        flipper_format_insert_or_update_hex(
            flipper_format, "Raw", instance->raw_bytes, FORD_V3_DATA_BYTES);

        pp_flipper_update_or_insert_u32(flipper_format, FF_SERIAL, instance->generic.serial);
        pp_flipper_update_or_insert_u32(flipper_format, FF_BTN, instance->generic.btn);
        pp_flipper_update_or_insert_u32(flipper_format, FF_CNT, instance->counter);
    }

    return ret;
}

SubGhzProtocolStatus
    subghz_protocol_decoder_ford_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);

    SubGhzProtocolDecoderFordV3* instance = context;

    SubGhzProtocolStatus ret = subghz_block_generic_deserialize_check_count_bit(
        &instance->generic,
        flipper_format,
        subghz_protocol_ford_v3_const.min_count_bit_for_found);

    if(ret != SubGhzProtocolStatusOk) {
        return ret;
    }

    const uint64_t d = instance->generic.data;
    for(uint8_t i = 0; i < 8U; i++) {
        instance->raw_bytes[i] = (uint8_t)(d >> (56 - i * 8));
    }
    memset(&instance->raw_bytes[8], 0, FORD_V3_DATA_BYTES - 8U);

    flipper_format_rewind(flipper_format);
    flipper_format_read_hex(flipper_format, "Raw", instance->raw_bytes, FORD_V3_DATA_BYTES);

    instance->bit_count = FORD_V3_DATA_BITS;
    ford_v3_parse_fields(instance);

    return ret;
}

void subghz_protocol_decoder_ford_v3_get_string(void* context, FuriString* output) {
    furi_check(context);

    SubGhzProtocolDecoderFordV3* instance = context;
    const uint8_t* k = instance->raw_bytes;

    furi_string_cat_printf(
        output,
        "%s %dbit\r\n"
        "Key:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n"
        "Sn:%08lX Btn:%02X %s\r\n"
        "Cnt:%04X Hop:%02X%02X%02X%02X\r\n",
        instance->generic.protocol_name,
        (int)instance->generic.data_count_bit,
        k[0],
        k[1],
        k[2],
        k[3],
        k[4],
        k[5],
        k[6],
        k[7],
        k[8],
        k[9],
        k[10],
        k[11],
        k[12],
        (unsigned long)instance->generic.serial,
        instance->generic.btn,
        ford_v3_button_name(instance->generic.btn),
        (unsigned)instance->counter,
        k[9],
        k[10],
        k[11],
        k[12]);
}

const SubGhzProtocolDecoder subghz_protocol_ford_v3_decoder = {
    .alloc = subghz_protocol_decoder_ford_v3_alloc,
    .free = pp_decoder_free_default,
    .feed = subghz_protocol_decoder_ford_v3_feed,
    .reset = subghz_protocol_decoder_ford_v3_reset,
    .get_hash_data = subghz_protocol_decoder_ford_v3_get_hash_data,
    .serialize = subghz_protocol_decoder_ford_v3_serialize,
    .deserialize = subghz_protocol_decoder_ford_v3_deserialize,
    .get_string = subghz_protocol_decoder_ford_v3_get_string,
};

#ifdef ENABLE_EMULATE_FEATURE
// =========================================================================
// ENCODER
// =========================================================================

// Preamble: FORD_V3_PREAMBLE_MIN(30)+1 = 31 short pulses, alternating
// Manchester data: 104 bits × 2 = 208 transitions
// Total per burst: 31 + 1 (long transition) + 208 = 240
#define FV3_ENCODER_BURSTS         6U
#define FV3_ENCODER_PREAMBLE       31U
#define FV3_ENCODER_INTER_BURST_US (FORD_V3_TE_LONG * 100U)
#define FV3_ENCODER_UPLOAD_CAPACITY \
    ((FV3_ENCODER_PREAMBLE + 1U + FORD_V3_DATA_BITS * 2U + 2U) * FV3_ENCODER_BURSTS + 5U)
_Static_assert(
    FV3_ENCODER_UPLOAD_CAPACITY <= PP_SHARED_UPLOAD_CAPACITY,
    "FV3_ENCODER_UPLOAD_CAPACITY exceeds shared upload slab");

typedef struct SubGhzProtocolEncoderFordV3 {
    SubGhzProtocolEncoderBase base;
    SubGhzProtocolBlockEncoder encoder;
    SubGhzBlockGeneric generic;

    uint8_t raw_bytes[FORD_V3_DATA_BYTES];
} SubGhzProtocolEncoderFordV3;

static void
    subghz_protocol_encoder_ford_v3_get_upload(SubGhzProtocolEncoderFordV3* instance) {
    furi_check(instance);

    // Differential Manchester emission matching the ford_v0 pattern.
    // The preamble ends on HIGH (31 pulses, starts HIGH, odd count),
    // so the first Manchester event is a LOW pulse.
    // We use pp_emit_manchester_bit for each bit: bit=true → HIGH,te / LOW,te
    // bit=false → LOW,te / HIGH,te  (G.E. Thomas: 1=low→high, 0=high→low)
    const uint32_t te = FORD_V3_TE_SHORT;
    size_t index = 0;

#define ADD_LEVEL(lvl, dur) \
    index = pp_emit_merge(instance->encoder.upload, index, FV3_ENCODER_UPLOAD_CAPACITY, (lvl), (dur))

    for(uint8_t burst = 0; burst < FV3_ENCODER_BURSTS; burst++) {
        // Preamble: 31 alternating short pulses starting HIGH
        for(uint8_t p = 0; p < FV3_ENCODER_PREAMBLE; p++) {
            ADD_LEVEL((p & 1U) == 0U, te);
        }
        // Preamble ends HIGH (pulse 30, 0-indexed, even=HIGH).
        // Emit one long LOW to trigger preamble→data transition (LongLow event).
        ADD_LEVEL(false, FORD_V3_TE_LONG);

        // Manchester data: 104 bits MSB first per byte
        for(uint8_t byte_i = 0; byte_i < FORD_V3_DATA_BYTES; byte_i++) {
            for(int8_t bit_i = 7; bit_i >= 0; bit_i--) {
                bool bit = (instance->raw_bytes[byte_i] >> bit_i) & 1U;
                index = pp_emit_manchester_bit(
                    instance->encoder.upload, index, FV3_ENCODER_UPLOAD_CAPACITY, bit, te);
            }
        }

        if(burst < FV3_ENCODER_BURSTS - 1U) {
            ADD_LEVEL(false, FV3_ENCODER_INTER_BURST_US);
        }
    }

#undef ADD_LEVEL

    instance->encoder.size_upload = index;
    instance->encoder.front = 0;
}

void* subghz_protocol_encoder_ford_v3_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolEncoderFordV3* instance = malloc(sizeof(SubGhzProtocolEncoderFordV3));
    furi_check(instance);
    instance->base.protocol = &ford_protocol_v3;
    instance->generic.protocol_name = instance->base.protocol->name;
    instance->encoder.repeat = 1;
    pp_encoder_buffer_ensure(instance, FV3_ENCODER_UPLOAD_CAPACITY);
    instance->encoder.is_running = false;
    instance->encoder.front = 0;
    memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));
    return instance;
}

SubGhzProtocolStatus
    subghz_protocol_encoder_ford_v3_deserialize(void* context, FlipperFormat* flipper_format) {
    furi_check(context);
    SubGhzProtocolEncoderFordV3* instance = context;
    SubGhzProtocolStatus ret = SubGhzProtocolStatusError;

    instance->encoder.is_running = false;
    instance->encoder.front = 0;

    do {
        if(pp_verify_protocol_name(flipper_format, instance->base.protocol->name) !=
           SubGhzProtocolStatusOk) {
            break;
        }

        // Read the raw bytes stored at serialization time
        memset(instance->raw_bytes, 0, sizeof(instance->raw_bytes));

        // Try to read from "Raw" hex field; fall back to reconstructing from serial/cnt/btn
        flipper_format_rewind(flipper_format);
        bool has_raw =
            flipper_format_read_hex(flipper_format, "Raw", instance->raw_bytes, FORD_V3_DATA_BYTES);

        if(!has_raw) {
            // Reconstruct from decoded fields
            uint32_t serial = UINT32_MAX, btn = UINT32_MAX, cnt = UINT32_MAX;
            pp_encoder_read_fields(flipper_format, &serial, &btn, &cnt, NULL);
            if(serial == UINT32_MAX) break;

            instance->raw_bytes[1] = (uint8_t)((serial >> 24) & 0xFF);
            instance->raw_bytes[2] = (uint8_t)((serial >> 16) & 0xFF);
            instance->raw_bytes[3] = (uint8_t)((serial >> 8) & 0xFF);
            instance->raw_bytes[4] = (uint8_t)(serial & 0xFF);
            instance->raw_bytes[6] = (btn == FORD_V3_BTN_UNLOCK) ? 0x01U : 0x00U;
            instance->raw_bytes[7] = (uint8_t)(~((cnt >> 8) & 0xFF));
            instance->raw_bytes[8] = (uint8_t)(~(cnt & 0xFF));
        }

        instance->encoder.repeat = pp_encoder_read_repeat(flipper_format, 1);

        subghz_protocol_encoder_ford_v3_get_upload(instance);

        if(instance->encoder.size_upload == 0) break;

        instance->encoder.is_running = true;
        ret = SubGhzProtocolStatusOk;
    } while(false);

    return ret;
}

const SubGhzProtocolEncoder subghz_protocol_ford_v3_encoder = {
    .alloc = subghz_protocol_encoder_ford_v3_alloc,
    .free = pp_encoder_free,
    .deserialize = subghz_protocol_encoder_ford_v3_deserialize,
    .stop = pp_encoder_stop,
    .yield = pp_encoder_yield,
};
#else
const SubGhzProtocolEncoder subghz_protocol_ford_v3_encoder = {
    .alloc = NULL,
    .free = NULL,
    .deserialize = NULL,
    .stop = NULL,
    .yield = NULL,
};
#endif

const SubGhzProtocol ford_protocol_v3 = {
    .name = FORD_PROTOCOL_V3_NAME,
    .type = SubGhzProtocolTypeDynamic,
    .flag = SubGhzProtocolFlag_315 | SubGhzProtocolFlag_433 | SubGhzProtocolFlag_FM |
            SubGhzProtocolFlag_Decodable |
            SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save
#ifdef ENABLE_EMULATE_FEATURE
            | SubGhzProtocolFlag_Send
#endif
    ,
    .decoder = &subghz_protocol_ford_v3_decoder,
    .encoder = &subghz_protocol_ford_v3_encoder,
};
