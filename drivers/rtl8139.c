/* ===========================================================================
 * RTL8139 Network Interface Card Driver
 * PCI-based 10/100 Mbps Ethernet controller
 * Uses programmed I/O with interrupt-driven receive
 * =========================================================================== */
#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "memory.h"
#include "string.h"

/* RTL8139 register offsets from I/O base */
#define REG_IDR0    0x00  /* MAC address bytes 0-5 */
#define REG_MAR0    0x08  /* Multicast filter */
#define REG_TSD0    0x10  /* Tx status descriptor 0 (+ 4*n for 1-3) */
#define REG_TSAD0   0x20  /* Tx start address descriptor 0 (+ 4*n) */
#define REG_RBSTART 0x30  /* Rx buffer start (physical address) */
#define REG_CMD     0x37  /* Command register */
#define REG_CAPR    0x38  /* Current address of packet read */
#define REG_CBR     0x3A  /* Current buffer address (write pointer) */
#define REG_IMR     0x3C  /* Interrupt mask register */
#define REG_ISR     0x3E  /* Interrupt status register */
#define REG_TCR     0x40  /* Tx configuration */
#define REG_RCR     0x44  /* Rx configuration */
#define REG_CONFIG1 0x52  /* Configuration register 1 */

/* Command register bits */
#define CMD_RESET   0x10
#define CMD_RX_EN   0x08
#define CMD_TX_EN   0x04

/* Interrupt status/mask bits */
#define INT_ROK     0x0001  /* Rx OK */
#define INT_TOK     0x0004  /* Tx OK */
#define INT_RER     0x0002  /* Rx error */
#define INT_TER     0x0008  /* Tx error */

/* Rx configuration */
#define RCR_AAP     0x01  /* Accept all packets (promiscuous) */
#define RCR_APM     0x02  /* Accept physical match */
#define RCR_AM      0x04  /* Accept multicast */
#define RCR_AB      0x08  /* Accept broadcast */
#define RCR_WRAP    0x80  /* Wrap around Rx buffer */

/* Tx status bits */
#define TSD_OWN     (1 << 13)  /* DMA completed */
#define TSD_TOK     (1 << 15)  /* Tx OK */

/* Buffer sizes */
#define RX_BUF_SIZE  (8192 + 16 + 1500)  /* 8K + header + max packet */
#define TX_BUF_SIZE  1536
#define TX_DESC_COUNT 4

/* Driver state */
static uint16_t io_base;
static uint8_t  mac_addr[6];
static uint8_t *rx_buffer;
static uint8_t *tx_buffers;
static uint8_t  current_tx;
static uint16_t rx_offset;
static uint8_t  irq_line;
static bool     initialized;
static rtl8139_rx_callback_t rx_callback;

/* ISR stub from kernel_entry.asm */
extern void isr_stub_network(void);

/* IDT gate registration (exported from idt.c) */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

/* Debug output */
static void nic_dbg(const char *s) {
    while (*s)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s++), "Nd"((uint16_t)0xE9));
}

static void nic_dbg_hex8(uint8_t v) {
    const char hex[] = "0123456789ABCDEF";
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hex[v >> 4]), "Nd"((uint16_t)0xE9));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)hex[v & 0xF]), "Nd"((uint16_t)0xE9));
}

bool rtl8139_init(void) {
    struct pci_device *dev = pci_find_device(0x10EC, 0x8139);
    if (!dev) {
        nic_dbg("RTL: not found\n");
        return false;
    }

    /* Get I/O base from BAR0 (bit 0 = I/O space indicator) */
    io_base = (uint16_t)(dev->bar[0] & 0xFFFC);
    irq_line = dev->irq_line;

    nic_dbg("RTL: IO=0x");
    nic_dbg_hex8((uint8_t)(io_base >> 8));
    nic_dbg_hex8((uint8_t)(io_base & 0xFF));
    nic_dbg(" IRQ=");
    if (irq_line >= 10)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)('0' + irq_line / 10)), "Nd"((uint16_t)0xE9));
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)('0' + irq_line % 10)), "Nd"((uint16_t)0xE9));
    nic_dbg("\n");

    /* Enable PCI bus mastering for DMA */
    pci_enable_bus_mastering(dev->bus, dev->dev, dev->func);

    /* Enable PCI I/O space access */
    uint32_t cmd = pci_config_read32(dev->bus, dev->dev, dev->func, 0x04);
    cmd |= 0x01;  /* I/O Space Enable */
    pci_config_write32(dev->bus, dev->dev, dev->func, 0x04, cmd);

    /* Power on the device */
    outb(io_base + REG_CONFIG1, 0x00);

    /* Software reset */
    outb(io_base + REG_CMD, CMD_RESET);
    for (int i = 0; i < 100000; i++) {
        if (!(inb(io_base + REG_CMD) & CMD_RESET)) break;
    }

    /* Read MAC address */
    for (int i = 0; i < 6; i++)
        mac_addr[i] = inb(io_base + REG_IDR0 + i);

    nic_dbg("RTL: MAC=");
    for (int i = 0; i < 6; i++) {
        nic_dbg_hex8(mac_addr[i]);
        if (i < 5) nic_dbg(":");
    }
    nic_dbg("\n");

    /* Allocate Rx buffer (must be physically contiguous — kmalloc is fine since no paging) */
    rx_buffer = (uint8_t *)kmalloc(RX_BUF_SIZE);
    if (!rx_buffer) { nic_dbg("RTL: rx alloc fail\n"); return false; }
    mem_set(rx_buffer, 0, RX_BUF_SIZE);

    /* Allocate Tx buffers */
    tx_buffers = (uint8_t *)kmalloc(TX_BUF_SIZE * TX_DESC_COUNT);
    if (!tx_buffers) { kfree(rx_buffer); nic_dbg("RTL: tx alloc fail\n"); return false; }
    mem_set(tx_buffers, 0, TX_BUF_SIZE * TX_DESC_COUNT);

    current_tx = 0;
    rx_offset = 0;
    rx_callback = NULL;

    /* Set Rx buffer address */
    outl(io_base + REG_RBSTART, (uint32_t)rx_buffer);

    /* Set interrupt mask: Rx OK, Tx OK, Rx error */
    outw(io_base + REG_IMR, INT_ROK | INT_TOK | INT_RER);

    /* Configure Rx: accept broadcast + physical match, WRAP mode, 8K buffer */
    outl(io_base + REG_RCR, RCR_AB | RCR_APM | RCR_AM | RCR_WRAP);

    /* Configure Tx: default DMA burst, interframe gap */
    outl(io_base + REG_TCR, 0x03000700);

    /* Enable Rx and Tx */
    outb(io_base + REG_CMD, CMD_RX_EN | CMD_TX_EN);

    /* Register IRQ handler in IDT */
    idt_set_gate(32 + irq_line, (uint32_t)isr_stub_network, 0x08, 0x8E);

    /* Unmask IRQ in PIC */
    if (irq_line >= 8) {
        uint8_t mask = inb(0xA1);
        mask &= ~(1 << (irq_line - 8));
        outb(0xA1, mask);
    } else {
        uint8_t mask = inb(0x21);
        mask &= ~(1 << irq_line);
        outb(0x21, mask);
    }

    initialized = true;
    nic_dbg("RTL: ready\n");
    return true;
}

void rtl8139_get_mac(uint8_t mac[6]) {
    mem_copy(mac, mac_addr, 6);
}

void rtl8139_set_rx_callback(rtl8139_rx_callback_t cb) {
    rx_callback = cb;
}

int rtl8139_send(const void *data, uint16_t len) {
    if (!initialized || len > TX_BUF_SIZE) return -1;

    /* Copy packet to Tx buffer */
    uint8_t *buf = tx_buffers + current_tx * TX_BUF_SIZE;
    mem_copy(buf, data, len);

    /* Set Tx start address */
    outl(io_base + REG_TSAD0 + current_tx * 4, (uint32_t)buf);

    /* Set size and start transmission (clear OWN bit by writing size) */
    outl(io_base + REG_TSD0 + current_tx * 4, len);

    /* Wait for transmission to complete (poll with timeout) */
    for (int i = 0; i < 100000; i++) {
        uint32_t status = inl(io_base + REG_TSD0 + current_tx * 4);
        if (status & TSD_TOK) break;
        if (status & TSD_OWN) break;
    }

    current_tx = (current_tx + 1) % TX_DESC_COUNT;
    return 0;
}

/* Interrupt handler — called from ISR stub in kernel_entry.asm */
void rtl8139_handler(void) {
    if (!initialized) {
        /* Send EOI just in case */
        if (irq_line >= 8) outb(0xA0, 0x20);
        outb(0x20, 0x20);
        return;
    }

    uint16_t status = inw(io_base + REG_ISR);

    /* Acknowledge all interrupts */
    outw(io_base + REG_ISR, status);

    if (status & INT_ROK) {
        /* Process received packets (limit iterations to prevent infinite loop) */
        int max_packets = 64;
        while (!(inb(io_base + REG_CMD) & 0x01) && max_packets-- > 0) {  /* Buffer not empty */
            /* RTL8139 Rx packet header: 4 bytes (status[16] + length[16]) */
            uint32_t *header = (uint32_t *)(rx_buffer + rx_offset);
            uint16_t rx_status = (uint16_t)(*header & 0xFFFF);
            uint16_t rx_size   = (uint16_t)((*header >> 16) & 0xFFFF);

            /* Sanity check */
            if (rx_size == 0 || rx_size > 1518 + 4 || !(rx_status & 0x01)) {
                /* Bad packet — reset Rx */
                outb(io_base + REG_CMD, CMD_TX_EN);
                outl(io_base + REG_RCR, RCR_AB | RCR_APM | RCR_AM | RCR_WRAP);
                outb(io_base + REG_CMD, CMD_RX_EN | CMD_TX_EN);
                rx_offset = inw(io_base + REG_CBR) % 8192;
                break;
            }

            /* Packet data starts after the 4-byte header */
            uint8_t *packet = rx_buffer + rx_offset + 4;

            if (rx_callback && rx_size >= 14) {
                /* Subtract 4 for CRC at end */
                uint16_t pkt_len = rx_size - 4;
                rx_callback(packet, pkt_len);
            }

            /* Advance read pointer: align to DWORD boundary */
            rx_offset = (rx_offset + rx_size + 4 + 3) & ~3;
            rx_offset %= 8192;

            /* Update CAPR (the -16 is an RTL8139 hardware quirk) */
            outw(io_base + REG_CAPR, rx_offset - 16);
        }
    }

    /* Send End Of Interrupt to PIC */
    if (irq_line >= 8)
        outb(0xA0, 0x20);  /* Slave PIC EOI */
    outb(0x20, 0x20);      /* Master PIC EOI */
}
