/* ===========================================================================
 * ATA PIO Driver
 * Simple polling-based ATA driver for primary bus (ports 0x1F0-0x1F7)
 * =========================================================================== */
#include "ata.h"
#include "io.h"

/* Primary ATA bus ports */
#define ATA_DATA       0x1F0
#define ATA_ERROR      0x1F1
#define ATA_FEATURES   0x1F1
#define ATA_SEC_COUNT  0x1F2
#define ATA_LBA_LOW    0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HIGH   0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS     0x1F7
#define ATA_COMMAND    0x1F7
#define ATA_ALT_STATUS 0x3F6

/* Status bits */
#define ATA_BSY   0x80
#define ATA_DRDY  0x40
#define ATA_DRQ   0x08
#define ATA_ERR   0x01

/* Commands */
#define ATA_CMD_READ     0x20
#define ATA_CMD_WRITE    0x30
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_FLUSH    0xE7

static bool detected = false;

/* Debug output via QEMU port 0xE9 */
static void ata_dbg(const char *s) {
    while (*s)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s++), "Nd"((uint16_t)0xE9));
}

/* 400ns delay: read alternate status 4 times */
static void ata_delay(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (!(s & ATA_BSY)) return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t s = inb(ATA_STATUS);
        if (s & ATA_ERR) return -1;
        if (!(s & ATA_BSY) && (s & ATA_DRQ)) return 0;
    }
    return -1;
}

bool ata_init(void) {
    detected = false;

    /* Select drive 0 (master) */
    outb(ATA_DRIVE_HEAD, 0xA0);
    ata_delay();

    /* Clear sector count and LBA registers */
    outb(ATA_SEC_COUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);

    /* Send IDENTIFY command */
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    uint8_t status = inb(ATA_STATUS);
    if (status == 0) {
        ata_dbg("ATA: no drive\n");
        return false;
    }

    /* Wait for BSY to clear */
    if (ata_wait_bsy() < 0) {
        ata_dbg("ATA: timeout\n");
        return false;
    }

    /* Check LBA mid/high — non-zero means ATAPI/SATA, not ATA */
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0) {
        ata_dbg("ATA: not ATA\n");
        return false;
    }

    /* Wait for DRQ */
    if (ata_wait_drq() < 0) {
        ata_dbg("ATA: identify err\n");
        return false;
    }

    /* Read and discard 256 words of IDENTIFY data */
    for (int i = 0; i < 256; i++)
        inw(ATA_DATA);

    detected = true;
    ata_dbg("ATA: drive ok\n");
    return true;
}

bool ata_available(void) {
    return detected;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer) {
    if (!detected) return -1;
    uint8_t *buf = (uint8_t *)buffer;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t sector = lba + i;

        if (ata_wait_bsy() < 0) return -1;

        outb(ATA_DRIVE_HEAD, 0xE0 | ((sector >> 24) & 0x0F));
        outb(ATA_SEC_COUNT, 1);
        outb(ATA_LBA_LOW, sector & 0xFF);
        outb(ATA_LBA_MID, (sector >> 8) & 0xFF);
        outb(ATA_LBA_HIGH, (sector >> 16) & 0xFF);
        outb(ATA_COMMAND, ATA_CMD_READ);

        ata_delay();
        if (ata_wait_drq() < 0) return -1;

        insw(ATA_DATA, buf, 256);
        buf += 512;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer) {
    if (!detected) return -1;
    const uint8_t *buf = (const uint8_t *)buffer;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t sector = lba + i;

        if (ata_wait_bsy() < 0) return -1;

        outb(ATA_DRIVE_HEAD, 0xE0 | ((sector >> 24) & 0x0F));
        outb(ATA_SEC_COUNT, 1);
        outb(ATA_LBA_LOW, sector & 0xFF);
        outb(ATA_LBA_MID, (sector >> 8) & 0xFF);
        outb(ATA_LBA_HIGH, (sector >> 16) & 0xFF);
        outb(ATA_COMMAND, ATA_CMD_WRITE);

        ata_delay();
        if (ata_wait_drq() < 0) return -1;

        outsw(ATA_DATA, buf, 256);
        buf += 512;

        /* Flush after each sector */
        outb(ATA_COMMAND, ATA_CMD_FLUSH);
        if (ata_wait_bsy() < 0) return -1;
    }
    return 0;
}
