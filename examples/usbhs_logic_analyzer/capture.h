/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LOGIC_CAPTURE_H
#define LOGIC_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "sump_protocol.h"

#define CAPTURE_RING_BYTES       (20U * 1024U)
#define CAPTURE_ADVERTISED_BYTES (16U * 1024U)
#define CAPTURE_MAX_RATE_HZ      10000000UL

enum capture_state {
    CAPTURE_IDLE = 0,
    CAPTURE_ARMED,
    CAPTURE_COMPLETE,
    CAPTURE_OVERFLOW
};

struct capture_result {
    uint32_t first_sample;
    uint32_t end_sample;
    uint32_t sample_count;
    uint32_t actual_rate_hz;
    uint8_t triggered;
};

void capture_init(void);
void capture_start(const struct sump_config *config);
void capture_abort(void);
void capture_poll(void);
enum capture_state capture_get_state(void);
const struct capture_result *capture_get_result(void);
uint8_t capture_get_sample(uint32_t absolute_index);

#endif
