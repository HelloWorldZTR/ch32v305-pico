/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "sump_protocol.h"

static void feed_long(struct sump_parser *parser, uint8_t opcode,
                      uint32_t value)
{
    assert(sump_parser_feed(parser, opcode) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(parser, (uint8_t)value) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(parser, (uint8_t)(value >> 8)) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(parser, (uint8_t)(value >> 16)) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(parser, (uint8_t)(value >> 24)) == SUMP_ACTION_NONE);
}

int main(void)
{
    struct sump_parser parser;
    uint8_t metadata[128];
    size_t metadata_length;

    sump_parser_init(&parser);
    assert(parser.config.read_count == 1024U);
    assert(sump_parser_feed(&parser, 0x02U) == SUMP_ACTION_SEND_ID);
    assert(sump_parser_feed(&parser, 0x04U) == SUMP_ACTION_SEND_METADATA);

    feed_long(&parser, 0x80U, 99U);
    assert(sump_requested_rate_hz(&parser.config) == 1000000U);

    assert(sump_parser_feed(&parser, 0x81U) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(&parser, 0xffU) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(&parser, 0x00U) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(&parser, 0x7fU) == SUMP_ACTION_NONE);
    assert(sump_parser_feed(&parser, 0x00U) == SUMP_ACTION_NONE);
    assert(parser.config.read_count == 1024U);
    assert(parser.config.delay_count == 512U);

    feed_long(&parser, 0xc0U, 0x000000a5U);
    feed_long(&parser, 0xc1U, 0x00000081U);
    feed_long(&parser, 0xc2U, 0x08000000U);
    assert(parser.config.trigger_mask == 0xa5U);
    assert(parser.config.trigger_value == 0x81U);
    assert(parser.config.trigger_config == 0x08000000U);

    /* Unsupported long commands are consumed without desynchronizing. */
    feed_long(&parser, 0xc4U, 0x12345678U);
    assert(sump_parser_feed(&parser, 0x01U) == SUMP_ACTION_RUN);

    metadata_length = sump_build_metadata(metadata, sizeof(metadata),
                                          16384U, 8000000U);
    assert(metadata_length > 0U);
    assert(metadata[0] == 0x01U);
    assert(metadata[metadata_length - 1U] == 0x00U);
    {
        size_t i;
        int found = 0;
        for (i = 0U; i + 8U <= metadata_length; i++) {
            if (memcmp(&metadata[i], "CH32V305", 8U) == 0) {
                found = 1;
                break;
            }
        }
        assert(found != 0);
    }

    assert(sump_parser_feed(&parser, 0x00U) == SUMP_ACTION_RESET);
    assert(parser.config.trigger_mask == 0U);
    assert(parser.config.read_count == 1024U);
    return 0;
}
