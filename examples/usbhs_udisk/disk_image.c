#include <stdint.h>

/*
 * Preformatted 8 KiB FAT12 image.  Keeping it in a dedicated linker section
 * avoids erasing/programming the same flash bank before USB enumeration.
 */
struct fat12_disk_image {
    uint8_t boot[512];
    uint8_t fat1[512];
    uint8_t fat2[512];
    uint8_t root[512];
    uint8_t data[12][512];
};

const struct fat12_disk_image usb_disk_image
    __attribute__((section(".diskimage"), used, aligned(4))) = {
        .boot = {
            [0] = 0xEB, [1] = 0x3C, [2] = 0x90,
            [3] = 'C', [4] = 'H', [5] = '3', [6] = '2',
            [7] = 'V', [8] = '3', [9] = '0', [10] = '5',
            [11] = 0x00, [12] = 0x02, /* 512 bytes/sector */
            [13] = 0x01,             /* one sector/cluster */
            [14] = 0x01, [15] = 0x00,
            [16] = 0x02,             /* two FATs */
            [17] = 0x10, [18] = 0x00,
            [19] = 0x10, [20] = 0x00, /* 16 sectors */
            [21] = 0xF8,
            [22] = 0x01, [23] = 0x00, /* one sector/FAT */
            [24] = 0x01, [25] = 0x00,
            [26] = 0x01, [27] = 0x00,
            [36] = 0x80,
            [38] = 0x29,
            [39] = 0xEF, [40] = 0xBE, [41] = 0x50, [42] = 0x30,
            [43] = 'C', [44] = 'H', [45] = '3', [46] = '2',
            [47] = 'V', [48] = '3', [49] = '0', [50] = '5',
            [51] = 'H', [52] = 'S', [53] = ' ',
            [54] = 'F', [55] = 'A', [56] = 'T', [57] = '1',
            [58] = '2', [59] = ' ', [60] = ' ', [61] = ' ',
            [510] = 0x55, [511] = 0xAA,
        },
        .fat1 = {[0] = 0xF8, [1] = 0xFF, [2] = 0xFF},
        .fat2 = {[0] = 0xF8, [1] = 0xFF, [2] = 0xFF},
        .root = {
            [0] = 'C', [1] = 'H', [2] = '3', [3] = '2',
            [4] = 'V', [5] = '3', [6] = '0', [7] = '5',
            [8] = 'H', [9] = 'S', [10] = ' ', [11] = 0x08,
        },
    };

typedef char disk_image_size_must_be_8k
    [(sizeof(usb_disk_image) == 8192U) ? 1 : -1];
