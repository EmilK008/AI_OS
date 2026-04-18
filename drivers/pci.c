/* ===========================================================================
 * PCI Bus Driver
 * Enumerates PCI devices on bus 0 using Configuration Space Access (Type 1)
 * =========================================================================== */
#include "pci.h"
#include "io.h"
#include "string.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define MAX_PCI_DEVICES 32

static struct pci_device devices[MAX_PCI_DEVICES];
static int num_devices = 0;

/* Debug output via QEMU debug port */
static void pci_dbg(const char *s) {
    while (*s)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s++), "Nd"((uint16_t)0xE9));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t cmd = pci_config_read32(bus, dev, func, 0x04);
    cmd |= (1 << 2);  /* Bus Master Enable */
    pci_config_write32(bus, dev, func, 0x04, cmd);
}

void pci_init(void) {
    num_devices = 0;
    mem_set(devices, 0, sizeof(devices));

    for (uint8_t dev = 0; dev < 32; dev++) {
        for (uint8_t func = 0; func < 8; func++) {
            uint32_t id = pci_config_read32(0, dev, func, 0x00);
            uint16_t vendor = id & 0xFFFF;
            uint16_t device = (id >> 16) & 0xFFFF;

            if (vendor == 0xFFFF) {
                if (func == 0) break;  /* No device at this slot */
                continue;
            }

            if (num_devices >= MAX_PCI_DEVICES) return;

            struct pci_device *d = &devices[num_devices];
            d->bus = 0;
            d->dev = dev;
            d->func = func;
            d->vendor_id = vendor;
            d->device_id = device;
            d->present = true;

            uint32_t class_reg = pci_config_read32(0, dev, func, 0x08);
            d->class_code = (class_reg >> 24) & 0xFF;
            d->subclass   = (class_reg >> 16) & 0xFF;

            /* Read BARs */
            for (int i = 0; i < 6; i++)
                d->bar[i] = pci_config_read32(0, dev, func, 0x10 + i * 4);

            /* Read IRQ line */
            d->irq_line = pci_config_read32(0, dev, func, 0x3C) & 0xFF;

            num_devices++;

            /* Single-function device? */
            if (func == 0) {
                uint32_t header = pci_config_read32(0, dev, func, 0x0C);
                if (!((header >> 16) & 0x80)) break;  /* Not multi-function */
            }
        }
    }

    pci_dbg("PCI: ");
    /* Print count as single digit or two digits */
    if (num_devices >= 10)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)('0' + num_devices / 10)), "Nd"((uint16_t)0xE9));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)('0' + num_devices % 10)), "Nd"((uint16_t)0xE9));
    pci_dbg(" devices\n");
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < num_devices; i++) {
        if (devices[i].vendor_id == vendor && devices[i].device_id == device)
            return &devices[i];
    }
    return NULL;
}

int pci_device_count(void) {
    return num_devices;
}
