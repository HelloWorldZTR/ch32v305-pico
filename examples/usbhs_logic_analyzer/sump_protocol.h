/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SUMP_PROTOCOL_H
#define SUMP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define SUMP_PROTOCOL_CLOCK_HZ 100000000UL
#define SUMP_DEFAULT_SAMPLES   1024UL

enum sump_action {
    SUMP_ACTION_NONE = 0,
    SUMP_ACTION_RESET,
    SUMP_ACTION_RUN,
    SUMP_ACTION_SEND_ID,
    SUMP_ACTION_SEND_METADATA
};

struct sump_config {
    uint32_t divider;
    uint32_t read_count;
    uint32_t delay_count;
    uint32_t flags;
    uint32_t trigger_mask;
    uint32_t trigger_value;
    uint32_t trigger_config;
};

struct sump_parser {
    struct sump_config config;
    uint8_t command[5];
    uint8_t command_length;
};

void sump_parser_init(struct sump_parser *parser);
enum sump_action sump_parser_feed(struct sump_parser *parser, uint8_t byte);
uint32_t sump_requested_rate_hz(const struct sump_config *config);
size_t sump_build_metadata(uint8_t *output, size_t capacity,
                           uint32_t sample_memory_bytes,
                           uint32_t max_sample_rate_hz);

#endif
