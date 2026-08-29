/**
 * @file usbd_msc_transport_test.c
 * @brief Portable MSC BOT/SCSI regression test for the internal-Flash disk.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CONFIG_USB_HS 1
#define CONFIG_USB_DBG_LEVEL (-1)

#include "usbd_core.h"
#include "usbd_msc.h"
#include "usb_scsi.h"

#define TEST_OUT_EP 0x03U
#define TEST_IN_EP  0x82U
#define TEST_BLOCK_COUNT 16U
#define TEST_BLOCK_SIZE  512U

static struct CBW test_cbw;
static uint8_t test_out_payload[TEST_BLOCK_SIZE];
static uint8_t test_block_buffer[TEST_BLOCK_SIZE];
static uint8_t test_last_in[TEST_BLOCK_SIZE];
static uint32_t test_last_in_len;
static uint32_t test_in_count;
static uint32_t test_read_count;
static uint32_t test_write_count;
static uint32_t test_last_read_sector;
static uint32_t test_last_write_sector;
static uint8_t test_stalled[256];

void usbd_class_register(usbd_class_t *devclass)
{
    (void)devclass;
}

void usbd_class_add_interface(usbd_class_t *devclass, usbd_interface_t *intf)
{
    (void)devclass;
    intf->intf_num = 0U;
}

void usbd_interface_add_endpoint(usbd_interface_t *intf, usbd_endpoint_t *ep)
{
    (void)intf;
    (void)ep;
}

int usbd_ep_set_stall(uint8_t ep)
{
    test_stalled[ep] = 1U;
    return 0;
}

int usbd_ep_write(uint8_t ep, const uint8_t *data, uint32_t data_len,
                  uint32_t *ret_bytes)
{
    assert(ep == TEST_IN_EP);
    assert(data_len <= sizeof(test_last_in));
    memcpy(test_last_in, data, data_len);
    test_last_in_len = data_len;
    test_in_count++;
    if(ret_bytes != NULL)
    {
        *ret_bytes = data_len;
    }
    return 0;
}

int usbd_ep_read(uint8_t ep, uint8_t *data, uint32_t max_data_len,
                 uint32_t *read_bytes)
{
    assert(ep == TEST_OUT_EP);
    if(max_data_len == 0U)
    {
        return 0;
    }
    if(max_data_len == USB_SIZEOF_MSC_CBW)
    {
        memcpy(data, &test_cbw, sizeof(test_cbw));
        *read_bytes = sizeof(test_cbw);
        return 0;
    }
    assert(max_data_len == TEST_BLOCK_SIZE);
    memcpy(data, test_out_payload, TEST_BLOCK_SIZE);
    *read_bytes = TEST_BLOCK_SIZE;
    return 0;
}

void usbd_msc_get_cap(uint8_t lun, uint32_t *block_num,
                      uint16_t *block_size)
{
    assert(lun == 0U);
    *block_num = TEST_BLOCK_COUNT;
    *block_size = TEST_BLOCK_SIZE;
}

int usbd_msc_sector_read(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    uint32_t i;

    assert(sector < TEST_BLOCK_COUNT);
    assert(length == TEST_BLOCK_SIZE);
    test_last_read_sector = sector;
    test_read_count++;
    for(i = 0U; i < length; i++)
    {
        buffer[i] = (uint8_t)(sector + i);
    }
    return 0;
}

int usbd_msc_sector_write(uint32_t sector, uint8_t *buffer, uint32_t length)
{
    assert(sector < TEST_BLOCK_COUNT);
    assert(length == TEST_BLOCK_SIZE);
    assert(memcmp(buffer, test_out_payload, TEST_BLOCK_SIZE) == 0);
    test_last_write_sector = sector;
    test_write_count++;
    return 0;
}

#include "../CherryUSB/class/msc/usbd_msc.c"

static void test_transport_reset(void)
{
    memset(&usbd_msc_cfg, 0, sizeof(usbd_msc_cfg));
    memset(&test_cbw, 0, sizeof(test_cbw));
    memset(test_last_in, 0, sizeof(test_last_in));
    memset(test_stalled, 0, sizeof(test_stalled));
    test_last_in_len = 0U;
    test_in_count = 0U;
    test_read_count = 0U;
    test_write_count = 0U;
    test_last_read_sector = 0U;
    test_last_write_sector = 0U;

    usbd_msc_cfg.stage = MSC_READ_CBW;
    usbd_msc_cfg.scsi_blk_nbr = TEST_BLOCK_COUNT;
    usbd_msc_cfg.scsi_blk_size = TEST_BLOCK_SIZE;
    usbd_msc_cfg.block_buffer = test_block_buffer;
    mass_ep_data[MSD_OUT_EP_IDX].ep_addr = TEST_OUT_EP;
    mass_ep_data[MSD_IN_EP_IDX].ep_addr = TEST_IN_EP;

    test_cbw.dSignature = MSC_CBW_Signature;
    test_cbw.dTag = 0x12345678U;
    test_cbw.bLUN = 0U;
}

static void test_mode_sense6(void)
{
    test_transport_reset();
    test_cbw.dDataLength = 4U;
    test_cbw.bmFlags = 0x80U;
    test_cbw.bCBLength = 6U;
    test_cbw.CB[0] = SCSI_CMD_MODESENSE6;
    test_cbw.CB[4] = 4U;

    mass_storage_bulk_out(TEST_OUT_EP);
    assert(test_in_count == 1U);
    assert(test_last_in_len == 4U);
    assert(test_last_in[0] == 3U);
    assert((test_last_in[2] & 0x80U) == 0U);
    assert(usbd_msc_cfg.stage == MSC_SEND_CSW);
}

static void test_synchronize_cache(void)
{
    const struct CSW *csw;

    test_transport_reset();
    test_cbw.dDataLength = 0U;
    test_cbw.bmFlags = 0x00U;
    test_cbw.bCBLength = 10U;
    test_cbw.CB[0] = SCSI_CMD_SYNCHCACHE10;

    mass_storage_bulk_out(TEST_OUT_EP);
    assert(test_last_in_len == USB_SIZEOF_MSC_CSW);
    csw = (const struct CSW *)test_last_in;
    assert(csw->dSignature == MSC_CSW_Signature);
    assert(csw->dTag == test_cbw.dTag);
    assert(csw->dDataResidue == 0U);
    assert(csw->bStatus == CSW_STATUS_CMD_PASSED);
    assert(usbd_msc_cfg.stage == MSC_WAIT_CSW);
}

static void test_read_last_sector(void)
{
    test_transport_reset();
    test_cbw.dDataLength = TEST_BLOCK_SIZE;
    test_cbw.bmFlags = 0x80U;
    test_cbw.bCBLength = 10U;
    test_cbw.CB[0] = SCSI_CMD_READ10;
    test_cbw.CB[5] = TEST_BLOCK_COUNT - 1U;
    test_cbw.CB[8] = 1U;

    mass_storage_bulk_out(TEST_OUT_EP);
    assert(test_read_count == 1U);
    assert(test_last_read_sector == TEST_BLOCK_COUNT - 1U);
    assert(test_last_in_len == TEST_BLOCK_SIZE);
    assert(test_last_in[0] == TEST_BLOCK_COUNT - 1U);
    assert(usbd_msc_cfg.stage == MSC_SEND_CSW);
}

static void test_write_sector(void)
{
    uint32_t i;
    const struct CSW *csw;

    test_transport_reset();
    for(i = 0U; i < TEST_BLOCK_SIZE; i++)
    {
        test_out_payload[i] = (uint8_t)(i ^ 0xA5U);
    }
    test_cbw.dDataLength = TEST_BLOCK_SIZE;
    test_cbw.bmFlags = 0x00U;
    test_cbw.bCBLength = 10U;
    test_cbw.CB[0] = SCSI_CMD_WRITE10;
    test_cbw.CB[5] = 2U;
    test_cbw.CB[8] = 1U;

    mass_storage_bulk_out(TEST_OUT_EP);
    assert(usbd_msc_cfg.stage == MSC_DATA_OUT);
    mass_storage_bulk_out(TEST_OUT_EP);
    assert(test_write_count == 1U);
    assert(test_last_write_sector == 2U);
    assert(test_last_in_len == USB_SIZEOF_MSC_CSW);
    csw = (const struct CSW *)test_last_in;
    assert(csw->bStatus == CSW_STATUS_CMD_PASSED);
    assert(usbd_msc_cfg.stage == MSC_WAIT_CSW);
}

static void test_wrapped_lba_rejected(void)
{
    test_transport_reset();
    test_cbw.dDataLength = TEST_BLOCK_SIZE;
    test_cbw.bmFlags = 0x80U;
    test_cbw.bCBLength = 12U;
    test_cbw.CB[0] = SCSI_CMD_READ12;
    test_cbw.CB[2] = 0xFFU;
    test_cbw.CB[3] = 0xFFU;
    test_cbw.CB[4] = 0xFFU;
    test_cbw.CB[5] = 0xFFU;
    test_cbw.CB[9] = 1U;

    mass_storage_bulk_out(TEST_OUT_EP);
    assert(test_read_count == 0U);
    assert(test_stalled[TEST_IN_EP] == 1U);
    assert(usbd_msc_cfg.sKey ==
           (uint8_t)(SCSI_KCQIR_LBAOUTOFRANGE >> 16));
}

/** @brief Exercise the MSC commands used to mount, write and safely eject. */
int main(void)
{
    test_mode_sense6();
    test_synchronize_cache();
    test_read_last_sector();
    test_write_sector();
    test_wrapped_lba_rejected();
    return 0;
}
