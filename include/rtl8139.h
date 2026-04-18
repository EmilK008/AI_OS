#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"

typedef void (*rtl8139_rx_callback_t)(const void *packet, uint16_t len);

bool rtl8139_init(void);
int  rtl8139_send(const void *data, uint16_t len);
void rtl8139_get_mac(uint8_t mac[6]);
void rtl8139_set_rx_callback(rtl8139_rx_callback_t cb);
void rtl8139_handler(void);

#endif
