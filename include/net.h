#ifndef NET_H
#define NET_H

#include "types.h"

/* Byte-order helpers (x86 is little-endian, network is big-endian) */
static inline uint16_t htons(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint16_t ntohs(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint32_t htonl(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000u);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

/* Network configuration */
struct net_config {
    uint32_t ip;
    uint32_t subnet;
    uint32_t gateway;
    uint32_t dns;
    uint8_t  mac[6];
    bool     configured;
};

void              net_init(void);
struct net_config *net_get_config(void);
bool              net_is_up(void);

/* ARP */
void net_arp_request(uint32_t target_ip);
bool net_arp_lookup(uint32_t ip, uint8_t mac_out[6]);
void net_arp_get_table(uint32_t *ips, uint8_t *macs, int *count, int max);

/* IPv4 */
int  net_send_ipv4(uint32_t dst_ip, uint8_t protocol, const void *payload, uint16_t len);

/* ICMP (ping) */
int  net_ping(uint32_t dst_ip);  /* returns RTT in ticks, or -1 on timeout */

/* UDP */
int  net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                  const void *data, uint16_t len);

/* DHCP */
bool net_dhcp_discover(void);

/* DNS */
bool net_dns_resolve(const char *hostname, uint32_t *ip_out);

/* TCP */
int  net_tcp_connect(uint32_t ip, uint16_t port);
int  net_tcp_send(const void *data, uint16_t len);
int  net_tcp_receive(void *buf, uint16_t max, uint32_t timeout_ticks);
void net_tcp_close(void);

/* HTTP */
int  net_http_get(const char *url, char *buf, int max_size);

/* IP parsing */
uint32_t net_parse_ip(const char *str);
void     net_format_ip(uint32_t ip, char *buf);

#endif
