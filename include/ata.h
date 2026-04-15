#ifndef ATA_H
#define ATA_H

#include "types.h"

/* Initialize ATA driver. Returns true if a drive is detected on primary bus. */
bool ata_init(void);

/* Returns true if ATA drive was detected during init. */
bool ata_available(void);

/* Read 'count' sectors starting at LBA into buffer.
   Buffer must hold count*512 bytes. Returns 0 on success, -1 on error. */
int ata_read_sectors(uint32_t lba, uint32_t count, void *buffer);

/* Write 'count' sectors starting at LBA from buffer.
   Buffer must hold count*512 bytes. Returns 0 on success, -1 on error. */
int ata_write_sectors(uint32_t lba, uint32_t count, const void *buffer);

#endif
