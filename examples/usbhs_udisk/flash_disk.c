#include "flash_disk.h"

#include <stddef.h>
#include <string.h>

#include "ch32v30x.h"
#include "ch32v30x_flash.h"

#define FLASH_PAGE_SIZE       256U
#define FLASH_PROGRAM_ALIAS   0x08000000UL
#define FLASH_DISK_SIZE       (FLASH_DISK_BLOCK_COUNT * FLASH_DISK_BLOCK_SIZE)

/* These absolute symbols are defined by link.ld. */
extern uint8_t _flash_disk_start[];
extern uint8_t _flash_disk_end[];

static uint32_t flash_page_buffer[FLASH_PAGE_SIZE / sizeof(uint32_t)]
    __attribute__((aligned(4)));
static volatile uint8_t write_failed;

static uintptr_t flash_disk_address(void)
{
    return (uintptr_t)_flash_disk_start;
}

static int flash_program_page(uintptr_t address, const uint8_t *data)
{
    memcpy(flash_page_buffer, data, FLASH_PAGE_SIZE);
    FLASH_ErasePage_Fast((uint32_t)(address | FLASH_PROGRAM_ALIAS));
    FLASH_ProgramPage_Fast((uint32_t)(address | FLASH_PROGRAM_ALIAS),
                           flash_page_buffer);
    return memcmp((const void *)address, flash_page_buffer,
                  FLASH_PAGE_SIZE) == 0 ? 0 : -1;
}

int flash_disk_read(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    uintptr_t address;

    if ((sector >= FLASH_DISK_BLOCK_COUNT) ||
        (length != FLASH_DISK_BLOCK_SIZE) || (buffer == NULL)) {
        return -1;
    }
    address = flash_disk_address() + sector * FLASH_DISK_BLOCK_SIZE;
    memcpy(buffer, (const void *)address, FLASH_DISK_BLOCK_SIZE);
    return 0;
}

int flash_disk_write(uint32_t sector, const uint8_t *buffer, uint32_t length)
{
    uintptr_t address;
    uint32_t interrupt_state;
    int result;

    if ((sector >= FLASH_DISK_BLOCK_COUNT) ||
        (length != FLASH_DISK_BLOCK_SIZE) || (buffer == NULL)) {
        return -1;
    }
    if (write_failed != 0U) {
        return -1;
    }

    address = flash_disk_address() + sector * FLASH_DISK_BLOCK_SIZE;

    if (memcmp((const void *)address, buffer, FLASH_DISK_BLOCK_SIZE) == 0) {
        return 0;
    }

    /* Match WCH's USBHS MSC Internal_Flash implementation, but preserve the
     * caller's interrupt state instead of unconditionally enabling IRQs. */
    interrupt_state = __get_MSTATUS();
    __disable_irq();
    FLASH_Unlock_Fast();
    result = flash_program_page(address, buffer);
    if (result == 0) {
        result = flash_program_page(address + FLASH_PAGE_SIZE,
                                    buffer + FLASH_PAGE_SIZE);
    }
    FLASH_Lock_Fast();
    __set_MSTATUS(interrupt_state);

    if (result != 0) {
        write_failed = 1U;
    }
    return result;
}

void flash_disk_init(void)
{
    if (((uintptr_t)_flash_disk_end - (uintptr_t)_flash_disk_start) !=
        FLASH_DISK_SIZE) {
        while (1) {
        }
    }

    write_failed = 0U;
}

int flash_disk_write_failed(void)
{
    return write_failed != 0U;
}
