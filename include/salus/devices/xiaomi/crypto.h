#ifndef SALUS_DEVICES_XIAOMI_CRYPTO_H
#define SALUS_DEVICES_XIAOMI_CRYPTO_H

/* Xiaomi SPP framing, auth/session crypto and minimal protobuf encoding (crypto.c). */

#include <glib.h>
#include <protobuf-c/protobuf-c.h>
#include <stdint.h>

uint16_t crc16_arc(const uint8_t *data, size_t len);
gboolean hmac_sha256(const uint8_t *key, size_t key_len,
                     const uint8_t *data, size_t data_len,
                     uint8_t out[32]);
gboolean compute_auth_step3_hmac(const uint8_t secret_key[16],
                                  const uint8_t phone_nonce[16],
                                  const uint8_t watch_nonce[16],
                                  uint8_t out64[64]);
gboolean aes_ccm_encrypt_4byte_tag(const uint8_t key[16],
                                   const uint8_t nonce12[12],
                                   const uint8_t *plaintext,
                                   size_t plaintext_len,
                                   uint8_t **out,
                                   size_t *out_len);
gboolean aes_ctr_crypt(const uint8_t key[16],
                       const uint8_t iv[16],
                       const uint8_t *input, size_t input_len,
                       uint8_t **out, size_t *out_len);
gboolean aes_ctr_encrypt(const uint8_t key[16],
                         const uint8_t iv[16],
                         const uint8_t *plaintext, size_t plaintext_len,
                         uint8_t **out, size_t *out_len);
void pb_append_varint(GByteArray *arr, uint64_t value);
void pb_append_tag(GByteArray *arr, uint32_t field, uint32_t wire_type);
void pb_append_len_bytes(GByteArray *arr, uint32_t field, const uint8_t *data, size_t len);
void pb_append_u32(GByteArray *arr, uint32_t field, uint32_t value);
void pb_append_float(GByteArray *arr, uint32_t field, float value);
gboolean pb_read_varint(const uint8_t *buf, size_t len, size_t *idx, uint64_t *out);
gboolean pb_skip_field(const uint8_t *buf, size_t len, size_t *idx, uint32_t wire_type);
void summarize_pb_message(const ProtobufCMessage *msg, GString *out, int depth);

#endif
