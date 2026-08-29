#ifndef FLASH_DISK_H
#define FLASH_DISK_H

#include <stdint.h>

#define FLASH_DISK_BLOCK_SIZE  512U
#define FLASH_DISK_BLOCK_COUNT 16U

void flash_disk_init(void);
int flash_disk_read(uint32_t sector, uint8_t *buffer, uint32_t length);
int flash_disk_write(uint32_t sector, const uint8_t *buffer, uint32_t length);
int flash_disk_write_failed(void);

#endif
