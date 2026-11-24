#include "salus/activity/mi_fitness_data.h"
#include "salus/activity/mi_fitness_validity.h"
#include "salus/util.h"

#include <string.h>

int mi_fitness_apk_daily_type(int wire_daily_subtype) {
    /* Band wire subtypes use the same daily-type IDs as Mi Fitness schemas (index.json). */
    return wire_daily_subtype;
}

bool mi_fitness_data_id_from_payload(const uint8_t *payload, size_t payload_len, int file_type, SalusFitnessDataId *out) {
    if (payload == NULL || out == NULL || payload_len < 7) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->timestamp_sec = salus_read_u32_le(payload);
    out->tz_15min = (int8_t)payload[4];
    out->version = payload[5];
    const uint8_t flags = payload[6];
    out->data_type = (flags & 0x80) != 0 ? 1 : 0;
    out->file_type = (uint8_t)(file_type >= 0 ? file_type : (flags & 3));
    const uint8_t subtype = (flags & 0x7f) >> 2;
    if (out->data_type == 0) {
        out->daily_type = subtype;
        out->sport_type = 0;
    } else {
        out->sport_type = subtype;
        out->daily_type = 0;
    }
    return true;
}

static int server_data_id_len(const SalusFitnessDataId *id) {
    return id->data_type == 0 ? 6 : 7;
}

bool mi_fitness_split_native_payload(const uint8_t *payload,
                                     size_t payload_len,
                                     const SalusFitnessDataId *id,
                                     SalusFitnessBinarySlice *out) {
    if (payload == NULL || id == NULL || out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    const int valid_len = mi_fitness_data_validity_len(id);
    if (valid_len < 0) {
        out->invalid_version = true;
        return false;
    }
    const int srv = server_data_id_len(id);
    const size_t header_len = (size_t)srv + 1u + (size_t)valid_len;
    if (payload_len < header_len) {
        return false;
    }
    const size_t prefix = header_len - (size_t)valid_len;
    out->data_valid = payload + prefix;
    out->data_valid_len = (size_t)valid_len;
    out->body = payload + header_len;
    out->body_len = payload_len - header_len;
    return true;
}
