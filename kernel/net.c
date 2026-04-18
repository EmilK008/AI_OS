/* ===========================================================================
 * Network Protocol Stack
 * Ethernet / ARP / IPv4 / ICMP / UDP / DHCP
 * =========================================================================== */
#include "net.h"
#include "rtl8139.h"
#include "io.h"
#include "memory.h"
#include "string.h"
#include "timer.h"

/* ---- Packet structures ---- */

struct eth_header {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} __attribute__((packed));

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_HDR_SIZE  14

struct arp_packet {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint8_t  spa[4];
    uint8_t  tha[6];
    uint8_t  tpa[4];
} __attribute__((packed));

struct ipv4_header {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

/* DHCP message */
struct dhcp_message {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed));

#define DHCP_MAGIC 0x63538263  /* 99.130.83.99 in little-endian dwords */

/* ---- State ---- */

static struct net_config config;
static bool net_up;

/* ARP table */
#define ARP_TABLE_SIZE 16
static struct {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
} arp_table[ARP_TABLE_SIZE];

/* Ping state (set by ISR, read by polling loop) */
static volatile bool    ping_reply_received;
static volatile uint16_t ping_reply_seq;
static volatile uint32_t ping_reply_tick;

/* DHCP state */
static volatile bool dhcp_offer_received;
static volatile bool dhcp_ack_received;
static uint32_t dhcp_offered_ip;
static uint32_t dhcp_server_ip;
static uint32_t dhcp_offered_subnet;
static uint32_t dhcp_offered_gateway;
static uint32_t dhcp_offered_dns;
static uint32_t dhcp_xid;

/* Packet ID counter */
static uint16_t ip_id_counter;

/* Ping sequence counter */
static uint16_t ping_seq;

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};

/* Debug */
static void net_dbg(const char *s) {
    while (*s)
        __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)*s++), "Nd"((uint16_t)0xE9));
}

/* ---- Checksum ---- */

static uint16_t checksum(const void *data, uint16_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1)
        sum += *(const uint8_t *)ptr;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ---- IP helpers ---- */

/* Store IP as 4 bytes (for ARP packets which use byte arrays) */
static void ip_to_bytes(uint32_t ip, uint8_t out[4]) {
    out[0] = (uint8_t)(ip & 0xFF);
    out[1] = (uint8_t)((ip >> 8) & 0xFF);
    out[2] = (uint8_t)((ip >> 16) & 0xFF);
    out[3] = (uint8_t)((ip >> 24) & 0xFF);
}

static uint32_t bytes_to_ip(const uint8_t b[4]) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint32_t net_parse_ip(const char *str) {
    uint8_t octets[4] = {0, 0, 0, 0};
    int octet_idx = 0;
    int val = 0;
    while (*str && octet_idx < 4) {
        if (*str == '.') {
            octets[octet_idx++] = (uint8_t)val;
            val = 0;
        } else if (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str - '0');
        }
        str++;
    }
    if (octet_idx < 4) octets[octet_idx] = (uint8_t)val;
    /* Network byte order: octets[0] is the MSB of the IP on the wire */
    return (uint32_t)octets[0] | ((uint32_t)octets[1] << 8) |
           ((uint32_t)octets[2] << 16) | ((uint32_t)octets[3] << 24);
}

void net_format_ip(uint32_t ip, char *buf) {
    uint8_t b[4];
    ip_to_bytes(ip, b);
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t v = b[i];
        if (v >= 100) buf[pos++] = '0' + v / 100;
        if (v >= 10)  buf[pos++] = '0' + (v / 10) % 10;
        buf[pos++] = '0' + v % 10;
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

/* ---- Ethernet ---- */

static int net_send_ethernet(const uint8_t dst[6], uint16_t ethertype,
                             const void *payload, uint16_t payload_len) {
    uint8_t frame[1514];
    if (payload_len > 1500) return -1;

    struct eth_header *eth = (struct eth_header *)frame;
    mem_copy(eth->dst, dst, 6);
    mem_copy(eth->src, config.mac, 6);
    eth->ethertype = htons(ethertype);
    mem_copy(frame + ETH_HDR_SIZE, payload, payload_len);

    /* Pad to minimum Ethernet frame size (60 bytes without FCS) */
    uint16_t frame_len = ETH_HDR_SIZE + payload_len;
    if (frame_len < 60) {
        mem_set(frame + frame_len, 0, 60 - frame_len);
        frame_len = 60;
    }

    return rtl8139_send(frame, frame_len);
}

/* ---- ARP ---- */

static void net_send_arp(uint16_t oper, const uint8_t *target_mac,
                         uint32_t sender_ip, uint32_t target_ip,
                         const uint8_t *dst_eth) {
    struct arp_packet arp;
    arp.htype = htons(1);       /* Ethernet */
    arp.ptype = htons(0x0800);  /* IPv4 */
    arp.hlen = 6;
    arp.plen = 4;
    arp.oper = htons(oper);
    mem_copy(arp.sha, config.mac, 6);
    ip_to_bytes(sender_ip, arp.spa);
    mem_copy(arp.tha, target_mac, 6);
    ip_to_bytes(target_ip, arp.tpa);

    net_send_ethernet(dst_eth, ETH_TYPE_ARP, &arp, sizeof(arp));
}

void net_arp_request(uint32_t target_ip) {
    net_send_arp(1, zero_mac, config.ip, target_ip, broadcast_mac);
}

bool net_arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
    /* Check table first */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            mem_copy(mac_out, arp_table[i].mac, 6);
            return true;
        }
    }

    /* Broadcast is always FF:FF:FF:FF:FF:FF */
    if (ip == 0xFFFFFFFF) {
        mem_copy(mac_out, broadcast_mac, 6);
        return true;
    }

    /* Send ARP request and wait */
    net_arp_request(ip);

    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 300) {  /* 3 second timeout */
        for (int i = 0; i < ARP_TABLE_SIZE; i++) {
            if (arp_table[i].valid && arp_table[i].ip == ip) {
                mem_copy(mac_out, arp_table[i].mac, 6);
                return true;
            }
        }
        __asm__ __volatile__("hlt");  /* Wait for next interrupt */
    }
    return false;
}

static void arp_table_add(uint32_t ip, const uint8_t mac[6]) {
    /* Update existing entry */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip) {
            mem_copy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    /* Find empty slot */
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) {
            arp_table[i].ip = ip;
            mem_copy(arp_table[i].mac, mac, 6);
            arp_table[i].valid = true;
            return;
        }
    }
    /* Table full — overwrite slot 0 */
    arp_table[0].ip = ip;
    mem_copy(arp_table[0].mac, mac, 6);
    arp_table[0].valid = true;
}

void net_arp_get_table(uint32_t *ips, uint8_t *macs, int *count, int max) {
    *count = 0;
    for (int i = 0; i < ARP_TABLE_SIZE && *count < max; i++) {
        if (arp_table[i].valid) {
            ips[*count] = arp_table[i].ip;
            mem_copy(macs + *count * 6, arp_table[i].mac, 6);
            (*count)++;
        }
    }
}

/* ---- Handle incoming ARP ---- */

static void net_handle_arp(const void *data, uint16_t len) {
    if (len < sizeof(struct arp_packet)) return;
    const struct arp_packet *arp = (const struct arp_packet *)data;

    if (ntohs(arp->htype) != 1 || ntohs(arp->ptype) != 0x0800) return;

    uint32_t sender_ip = bytes_to_ip(arp->spa);
    uint32_t target_ip = bytes_to_ip(arp->tpa);

    /* Always learn sender's MAC */
    arp_table_add(sender_ip, arp->sha);

    if (ntohs(arp->oper) == 1 && target_ip == config.ip && config.ip != 0) {
        /* ARP request for our IP — send reply */
        net_send_arp(2, arp->sha, config.ip, sender_ip, arp->sha);
    }
}

/* ---- IPv4 ---- */

int net_send_ipv4(uint32_t dst_ip, uint8_t protocol, const void *payload, uint16_t len) {
    uint8_t pkt[1500];
    if (len > 1500 - 20) return -1;

    struct ipv4_header *ip = (struct ipv4_header *)pkt;
    ip->ver_ihl    = 0x45;
    ip->tos        = 0;
    ip->total_len  = htons(20 + len);
    ip->id         = htons(ip_id_counter++);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = protocol;
    ip->checksum   = 0;
    ip->src_ip     = config.ip;
    ip->dst_ip     = dst_ip;
    ip->checksum   = checksum(ip, 20);

    mem_copy(pkt + 20, payload, len);

    /* Determine next hop */
    uint32_t next_hop;
    if (dst_ip == 0xFFFFFFFF) {
        /* Broadcast */
        return net_send_ethernet(broadcast_mac, ETH_TYPE_IPV4, pkt, 20 + len);
    } else if (config.subnet != 0 && (dst_ip & config.subnet) == (config.ip & config.subnet)) {
        next_hop = dst_ip;  /* Same subnet */
    } else {
        next_hop = config.gateway;  /* Route via gateway */
    }

    /* Resolve MAC via ARP */
    uint8_t dst_mac[6];
    if (!net_arp_lookup(next_hop, dst_mac)) {
        net_dbg("NET: ARP timeout\n");
        return -1;
    }

    return net_send_ethernet(dst_mac, ETH_TYPE_IPV4, pkt, 20 + len);
}

/* ---- Handle incoming IPv4 ---- */

static void net_handle_icmp(uint32_t src_ip, const void *data, uint16_t len);
static void net_handle_udp(uint32_t src_ip, const void *data, uint16_t len);

static void net_handle_ipv4(const void *data, uint16_t len) {
    if (len < 20) return;
    const struct ipv4_header *ip = (const struct ipv4_header *)data;

    uint8_t ihl = (ip->ver_ihl & 0x0F) * 4;
    uint16_t total = ntohs(ip->total_len);
    if (total > len) return;

    const void *payload = (const uint8_t *)data + ihl;
    uint16_t payload_len = total - ihl;

    if (ip->protocol == IP_PROTO_ICMP) {
        net_handle_icmp(ip->src_ip, payload, payload_len);
    } else if (ip->protocol == IP_PROTO_UDP) {
        net_handle_udp(ip->src_ip, payload, payload_len);
    }
}

/* ---- ICMP ---- */

int net_ping(uint32_t dst_ip) {
    uint8_t pkt[40];  /* 8 byte ICMP header + 32 bytes payload */
    struct icmp_header *icmp = (struct icmp_header *)pkt;

    ping_seq++;
    ping_reply_received = false;

    icmp->type = 8;  /* Echo request */
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(0x1234);
    icmp->sequence = htons(ping_seq);

    /* Fill payload with pattern */
    for (int i = 0; i < 32; i++)
        pkt[8 + i] = (uint8_t)i;

    icmp->checksum = checksum(pkt, 40);

    uint32_t send_tick = timer_get_ticks();
    if (net_send_ipv4(dst_ip, IP_PROTO_ICMP, pkt, 40) < 0)
        return -1;

    /* Wait for reply */
    while (timer_get_ticks() - send_tick < 300) {  /* 3 sec timeout */
        if (ping_reply_received && ping_reply_seq == ping_seq) {
            return (int)(ping_reply_tick - send_tick);
        }
        __asm__ __volatile__("hlt");
    }
    return -1;
}

static void net_handle_icmp(uint32_t src_ip, const void *data, uint16_t len) {
    if (len < 8) return;
    const struct icmp_header *icmp = (const struct icmp_header *)data;

    if (icmp->type == 0) {
        /* Echo reply */
        if (ntohs(icmp->id) == 0x1234) {
            ping_reply_seq = ntohs(icmp->sequence);
            ping_reply_tick = timer_get_ticks();
            ping_reply_received = true;
        }
    } else if (icmp->type == 8 && config.ip != 0) {
        /* Echo request — send reply */
        uint8_t reply[1500];
        if (len > 1480) return;
        mem_copy(reply, data, len);
        struct icmp_header *r = (struct icmp_header *)reply;
        r->type = 0;  /* Echo reply */
        r->checksum = 0;
        r->checksum = checksum(reply, len);
        net_send_ipv4(src_ip, IP_PROTO_ICMP, reply, len);
    }
}

/* ---- UDP ---- */

int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const void *data, uint16_t len) {
    uint8_t pkt[1480];
    if (len > 1480 - 8) return -1;

    struct udp_header *udp = (struct udp_header *)pkt;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length   = htons(8 + len);
    udp->checksum = 0;  /* Optional for IPv4 */

    mem_copy(pkt + 8, data, len);

    return net_send_ipv4(dst_ip, IP_PROTO_UDP, pkt, 8 + len);
}

/* ---- DHCP ---- */

static void dhcp_parse_options(const uint8_t *opts, uint16_t len) {
    uint16_t i = 0;
    while (i < len) {
        uint8_t type = opts[i++];
        if (type == 0xFF) break;    /* End */
        if (type == 0) continue;    /* Pad */
        if (i >= len) break;
        uint8_t olen = opts[i++];
        if (i + olen > len) break;

        switch (type) {
            case 1:   /* Subnet mask */
                if (olen == 4) dhcp_offered_subnet = bytes_to_ip(&opts[i]);
                break;
            case 3:   /* Router/gateway */
                if (olen >= 4) dhcp_offered_gateway = bytes_to_ip(&opts[i]);
                break;
            case 6:   /* DNS server */
                if (olen >= 4) dhcp_offered_dns = bytes_to_ip(&opts[i]);
                break;
            case 51:  /* Lease time — ignored */
                break;
            case 53:  /* DHCP message type */
                if (olen == 1) {
                    if (opts[i] == 2) dhcp_offer_received = true;   /* OFFER */
                    if (opts[i] == 5) dhcp_ack_received = true;     /* ACK */
                }
                break;
            case 54:  /* Server identifier */
                if (olen == 4) dhcp_server_ip = bytes_to_ip(&opts[i]);
                break;
        }
        i += olen;
    }
}

static void net_handle_udp(uint32_t src_ip, const void *data, uint16_t len) {
    (void)src_ip;
    if (len < 8) return;
    const struct udp_header *udp = (const struct udp_header *)data;
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t udp_len = ntohs(udp->length);
    if (udp_len > len) return;

    if (dst_port == 68) {
        /* DHCP response */
        const uint8_t *payload = (const uint8_t *)data + 8;
        uint16_t payload_len = udp_len - 8;
        if (payload_len < 240) return;

        const struct dhcp_message *dhcp = (const struct dhcp_message *)payload;
        if (dhcp->op != 2) return;       /* Must be reply */
        if (dhcp->xid != dhcp_xid) return; /* Must match our transaction */

        dhcp_offered_ip = dhcp->yiaddr;

        /* Parse options (after 240-byte fixed header) */
        if (payload_len > 240)
            dhcp_parse_options(dhcp->options, payload_len - 240);
    }
}

static void dhcp_send(uint8_t msg_type, uint32_t requested_ip, uint32_t server_ip) {
    struct dhcp_message dhcp;
    mem_set(&dhcp, 0, sizeof(dhcp));

    dhcp.op = 1;       /* Request */
    dhcp.htype = 1;    /* Ethernet */
    dhcp.hlen = 6;
    dhcp.xid = dhcp_xid;
    dhcp.flags = htons(0x8000);  /* Broadcast flag */
    mem_copy(dhcp.chaddr, config.mac, 6);
    dhcp.magic = 0x63538263;  /* DHCP magic cookie (99.130.83.99 as bytes) */

    /* Options */
    int i = 0;
    dhcp.options[i++] = 53;  /* DHCP message type */
    dhcp.options[i++] = 1;
    dhcp.options[i++] = msg_type;

    if (msg_type == 3) {
        /* DHCP REQUEST — include requested IP and server ID */
        dhcp.options[i++] = 50;  /* Requested IP */
        dhcp.options[i++] = 4;
        ip_to_bytes(requested_ip, &dhcp.options[i]);
        i += 4;

        dhcp.options[i++] = 54;  /* Server identifier */
        dhcp.options[i++] = 4;
        ip_to_bytes(server_ip, &dhcp.options[i]);
        i += 4;
    }

    dhcp.options[i++] = 255;  /* End */

    /* DHCP must be sent as broadcast at both IP and Ethernet level */
    /* Build UDP+IP manually since we might not have an IP yet */
    uint8_t pkt[600];
    struct ipv4_header *ip = (struct ipv4_header *)pkt;
    uint16_t udp_total = 8 + sizeof(dhcp);
    uint16_t ip_total = 20 + udp_total;

    ip->ver_ihl    = 0x45;
    ip->tos        = 0;
    ip->total_len  = htons(ip_total);
    ip->id         = htons(ip_id_counter++);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = IP_PROTO_UDP;
    ip->checksum   = 0;
    ip->src_ip     = 0;          /* 0.0.0.0 — we don't have an IP yet */
    ip->dst_ip     = 0xFFFFFFFF; /* 255.255.255.255 broadcast */
    ip->checksum   = checksum(ip, 20);

    struct udp_header *udp = (struct udp_header *)(pkt + 20);
    udp->src_port = htons(68);
    udp->dst_port = htons(67);
    udp->length   = htons(udp_total);
    udp->checksum = 0;

    mem_copy(pkt + 28, &dhcp, sizeof(dhcp));

    net_send_ethernet(broadcast_mac, ETH_TYPE_IPV4, pkt, ip_total);
}

bool net_dhcp_discover(void) {
    if (!net_up) return false;

    net_dbg("DHCP: discover\n");

    /* Generate transaction ID from tick counter */
    dhcp_xid = timer_get_ticks() ^ 0xDEAD0000;
    dhcp_offer_received = false;
    dhcp_ack_received = false;
    dhcp_offered_ip = 0;
    dhcp_offered_subnet = 0;
    dhcp_offered_gateway = 0;
    dhcp_offered_dns = 0;
    dhcp_server_ip = 0;

    /* Send DISCOVER */
    dhcp_send(1, 0, 0);

    /* Wait for OFFER */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 500) {  /* 5 second timeout */
        if (dhcp_offer_received) break;
        __asm__ __volatile__("hlt");
    }

    if (!dhcp_offer_received) {
        net_dbg("DHCP: no offer\n");
        return false;
    }

    net_dbg("DHCP: offer received\n");

    /* Send REQUEST */
    dhcp_ack_received = false;
    dhcp_send(3, dhcp_offered_ip, dhcp_server_ip);

    /* Wait for ACK */
    start = timer_get_ticks();
    while (timer_get_ticks() - start < 500) {
        if (dhcp_ack_received) break;
        __asm__ __volatile__("hlt");
    }

    if (!dhcp_ack_received) {
        net_dbg("DHCP: no ack\n");
        return false;
    }

    /* Apply configuration */
    config.ip = dhcp_offered_ip;
    config.subnet = dhcp_offered_subnet;
    config.gateway = dhcp_offered_gateway;
    config.dns = dhcp_offered_dns;
    config.configured = true;

    net_dbg("DHCP: configured\n");
    return true;
}

/* ---- Rx callback (called from RTL8139 ISR) ---- */

static void net_receive(const void *packet, uint16_t len) {
    if (len < ETH_HDR_SIZE) return;

    const struct eth_header *eth = (const struct eth_header *)packet;
    const void *payload = (const uint8_t *)packet + ETH_HDR_SIZE;
    uint16_t payload_len = len - ETH_HDR_SIZE;

    uint16_t ethertype = ntohs(eth->ethertype);

    if (ethertype == ETH_TYPE_ARP) {
        net_handle_arp(payload, payload_len);
    } else if (ethertype == ETH_TYPE_IPV4) {
        net_handle_ipv4(payload, payload_len);
    }
}

/* ---- Init ---- */

void net_init(void) {
    mem_set(&config, 0, sizeof(config));
    mem_set(arp_table, 0, sizeof(arp_table));
    rtl8139_get_mac(config.mac);
    rtl8139_set_rx_callback(net_receive);
    net_up = true;
    ping_seq = 0;
    ip_id_counter = 1;
    net_dbg("NET: stack ready\n");
}

struct net_config *net_get_config(void) {
    return &config;
}

bool net_is_up(void) {
    return net_up;
}
