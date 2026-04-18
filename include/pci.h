#ifndef PCI_H
#define PCI_H

#include "types.h"

struct pci_device {
    uint8_t  bus, dev, func;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass;
    uint32_t bar[6];
    uint8_t  irq_line;
    bool     present;
};

void     pci_init(void);
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
struct pci_device *pci_find_device(uint16_t vendor, uint16_t device);
void     pci_enable_bus_mastering(uint8_t bus, uint8_t dev, uint8_t func);
int      pci_device_count(void);

#endif
