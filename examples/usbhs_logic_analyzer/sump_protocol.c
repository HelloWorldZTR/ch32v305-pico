/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sump_protocol.h"

#include <string.h>

#define SUMP_CMD_RESET          0x00U
#define SUMP_CMD_RUN            0x01U
#define SUMP_CMD_ID             0x02U
#define SUMP_CMD_METADATA       0x04U
#define SUMP_CMD_SET_DIVIDER    0x80U
#define SUMP_CMD_CAPTURE_SIZE   0x81U
#define SUMP_CMD_SET_FLAGS      0x82U
#define SUMP_CMD_TRIGGER_MASK0  0xc0U
#define SUMP_CMD_TRIGGER_VALUE0 0xc1U
#define SUMP_CMD_TRIGGER_CFG0   0xc2U

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void sump_config_reset(struct sump_config *config)
{
    memset(config, 0, sizeof(*config));
    config->divider = (SUMP_PROTOCOL_CLOCK_HZ / 1000000UL) - 1UL;
    config->read_count = SUMP_DEFAULT_SAMPLES;
    config->delay_count = SUMP_DEFAULT_SAMPLES;
}

void sump_parser_init(struct sump_parser *parser)
{
    memset(parser, 0, sizeof(*parser));
    sump_config_reset(&parser->config);
}

static void sump_apply_long_command(struct sump_parser *parser)
{
    const uint8_t opcode = parser->command[0];
    const uint32_t value = read_le32(&parser->command[1]);

    switch (opcode) {
    case SUMP_CMD_SET_DIVIDER:
        parser->config.divider = value & 0x00ffffffUL;
        break;
    case SUMP_CMD_CAPTURE_SIZE:
        parser->config.read_count =
            (((uint32_t)parser->command[1] |
              ((uint32_t)parser->command[2] << 8)) + 1UL) * 4UL;
        parser->config.delay_count =
            (((uint32_t)parser->command[3] |
              ((uint32_t)parser->command[4] << 8)) + 1UL) * 4UL;
        break;
    case SUMP_CMD_SET_FLAGS:
        parser->config.flags = value;
        break;
    case SUMP_CMD_TRIGGER_MASK0:
        parser->config.trigger_mask = value;
        break;
    case SUMP_CMD_TRIGGER_VALUE0:
        parser->config.trigger_value = value;
        break;
    case SUMP_CMD_TRIGGER_CFG0:
        parser->config.trigger_config = value;
        break;
    default:
        /* Extended counts, RLE and trigger stages 1-3 are not advertised. */
        break;
    }
}

enum sump_action sump_parser_feed(struct sump_parser *parser, uint8_t byte)
{
    if (parser->command_length != 0U) {
        parser->command[parser->command_length++] = byte;
        if (parser->command_length == sizeof(parser->command)) {
            sump_apply_long_command(parser);
            parser->command_length = 0U;
        }
        return SUMP_ACTION_NONE;
    }

    if ((byte & 0x80U) != 0U) {
        parser->command[0] = byte;
        parser->command_length = 1U;
        return SUMP_ACTION_NONE;
    }

    switch (byte) {
    case SUMP_CMD_RESET:
        parser->command_length = 0U;
        sump_config_reset(&parser->config);
        return SUMP_ACTION_RESET;
    case SUMP_CMD_RUN:
        return SUMP_ACTION_RUN;
    case SUMP_CMD_ID:
        return SUMP_ACTION_SEND_ID;
    case SUMP_CMD_METADATA:
        return SUMP_ACTION_SEND_METADATA;
    default:
        return SUMP_ACTION_NONE;
    }
}

uint32_t sump_requested_rate_hz(const struct sump_config *config)
{
    return SUMP_PROTOCOL_CLOCK_HZ / ((config->divider & 0x00ffffffUL) + 1UL);
}

static int append_string(uint8_t **cursor, size_t *remaining,
                         uint8_t token, const char *value)
{
    const size_t length = strlen(value) + 1U;

    if (*remaining < length + 1U) {
        return -1;
    }
    *(*cursor)++ = token;
    memcpy(*cursor, value, length);
    *cursor += length;
    *remaining -= length + 1U;
    return 0;
}

static int append_u32(uint8_t **cursor, size_t *remaining,
                      uint8_t token, uint32_t value)
{
    if (*remaining < 5U) {
        return -1;
    }
    *(*cursor)++ = token;
    write_be32(*cursor, value);
    *cursor += 4;
    *remaining -= 5U;
    return 0;
}

size_t sump_build_metadata(uint8_t *output, size_t capacity,
                           uint32_t sample_memory_bytes,
                           uint32_t max_sample_rate_hz)
{
    uint8_t *cursor = output;
    size_t remaining = capacity;

    if (append_string(&cursor, &remaining, 0x01U,
                      "CH32V305 Pico Logic Analyzer") < 0 ||
        append_string(&cursor, &remaining, 0x03U, "0.1.0") < 0 ||
        append_u32(&cursor, &remaining, 0x21U, 8U) < 0 ||
        append_u32(&cursor, &remaining, 0x23U, sample_memory_bytes) < 0 ||
        append_u32(&cursor, &remaining, 0x24U, max_sample_rate_hz) < 0 ||
        append_u32(&cursor, &remaining, 0x20U, 1U) < 0 ||
        remaining == 0U) {
        return 0U;
    }
    *cursor++ = 0x00U;
    return (size_t)(cursor - output);
}
