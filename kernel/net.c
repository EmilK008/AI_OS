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
    static uint8_t frame[1514];
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
    static uint8_t pkt[1500];
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
static void net_handle_dns(const uint8_t *data, uint16_t len);
static void net_handle_tcp(uint32_t src_ip, const void *data, uint16_t len);

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
    } else if (ip->protocol == 6) {
        net_handle_tcp(ip->src_ip, payload, payload_len);
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
        static uint8_t reply[1500];
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
    static uint8_t pkt[1480];
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
    } else if (dst_port == 1053) {
        /* DNS response */
        const uint8_t *payload = (const uint8_t *)data + 8;
        uint16_t payload_len = udp_len - 8;
        net_handle_dns(payload, payload_len);
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
    static uint8_t pkt[600];
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

/* ---- DNS ---- */

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static volatile bool dns_reply_received;
static volatile uint32_t dns_resolved_ip;
static volatile uint16_t dns_reply_id;

static uint16_t dns_current_id;

/* Encode hostname into DNS wire format: "example.com" -> "\7example\3com\0" */
static int dns_encode_name(const char *name, uint8_t *out) {
    int pos = 0;
    while (*name) {
        int label_pos = pos++;
        int label_len = 0;
        while (*name && *name != '.') {
            out[pos++] = (uint8_t)*name++;
            label_len++;
        }
        out[label_pos] = (uint8_t)label_len;
        if (*name == '.') name++;
    }
    out[pos++] = 0;  /* Root label */
    return pos;
}

bool net_dns_resolve(const char *hostname, uint32_t *ip_out) {
    if (!config.configured || config.dns == 0) return false;

    static uint8_t pkt[512];
    mem_set(pkt, 0, 512);

    dns_current_id = (uint16_t)(timer_get_ticks() & 0xFFFF);
    dns_reply_received = false;

    struct dns_header *dns = (struct dns_header *)pkt;
    dns->id = htons(dns_current_id);
    dns->flags = htons(0x0100);  /* Standard query, recursion desired */
    dns->qdcount = htons(1);

    /* Encode question */
    int qpos = sizeof(struct dns_header);
    qpos += dns_encode_name(hostname, pkt + qpos);
    /* QTYPE = A (1), QCLASS = IN (1) */
    pkt[qpos++] = 0; pkt[qpos++] = 1;
    pkt[qpos++] = 0; pkt[qpos++] = 1;

    net_send_udp(config.dns, 1053, 53, pkt, (uint16_t)qpos);

    /* Wait for reply */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 300) {
        if (dns_reply_received && dns_reply_id == dns_current_id) {
            *ip_out = dns_resolved_ip;
            return true;
        }
        __asm__ __volatile__("hlt");
    }
    return false;
}

static void net_handle_dns(const uint8_t *data, uint16_t len) {
    if (len < sizeof(struct dns_header)) return;
    const struct dns_header *dns = (const struct dns_header *)data;

    uint16_t id = ntohs(dns->id);
    uint16_t ancount = ntohs(dns->ancount);
    if (ancount == 0) return;

    /* Skip question section */
    int pos = sizeof(struct dns_header);
    uint16_t qdcount = ntohs(dns->qdcount);
    for (uint16_t q = 0; q < qdcount && pos < len; q++) {
        while (pos < len && data[pos] != 0) {
            if ((data[pos] & 0xC0) == 0xC0) { pos += 2; goto qskipped; }
            pos += data[pos] + 1;
        }
        pos++;  /* Skip null terminator */
        qskipped:
        pos += 4;  /* Skip QTYPE + QCLASS */
    }

    /* Parse answer records */
    for (uint16_t a = 0; a < ancount && pos + 12 <= len; a++) {
        /* Skip name (possibly compressed) */
        if ((data[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < len && data[pos] != 0) pos += data[pos] + 1;
            pos++;
        }

        if (pos + 10 > len) break;
        uint16_t rtype = ((uint16_t)data[pos] << 8) | data[pos + 1];
        uint16_t rdlen = ((uint16_t)data[pos + 8] << 8) | data[pos + 9];
        pos += 10;

        if (rtype == 1 && rdlen == 4 && pos + 4 <= len) {
            /* A record — IPv4 address */
            dns_resolved_ip = bytes_to_ip(&data[pos]);
            dns_reply_id = id;
            dns_reply_received = true;
            return;
        }
        pos += rdlen;
    }
}

/* ---- TCP ---- */

#define TCP_STATE_CLOSED     0
#define TCP_STATE_SYN_SENT   1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT1  3
#define TCP_STATE_FIN_WAIT2  4
#define TCP_STATE_CLOSING    5
#define TCP_STATE_TIME_WAIT  6

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;  /* Upper 4 bits = header length in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10

/* TCP pseudo-header for checksum */
struct tcp_pseudo {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_len;
} __attribute__((packed));

/* Single TCP connection state */
static struct {
    int      state;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq;        /* Our next sequence number */
    uint32_t ack;        /* Next expected byte from remote */
    uint32_t initial_seq;
    uint8_t  remote_mac[6]; /* Cached next-hop MAC (resolved at connect time) */

    /* Receive buffer */
    uint8_t  rx_buf[16384];
    volatile uint16_t rx_len;
    volatile bool rx_fin;
} tcp;

static uint16_t tcp_checksum(const void *tcp_data, uint16_t tcp_len,
                             uint32_t src_ip, uint32_t dst_ip) {
    /* Build pseudo-header as raw bytes to avoid struct layout issues */
    uint8_t pseudo[12];
    mem_copy(&pseudo[0], &src_ip, 4);
    mem_copy(&pseudo[4], &dst_ip, 4);
    pseudo[8] = 0;
    pseudo[9] = 6;  /* TCP protocol */
    pseudo[10] = (uint8_t)(tcp_len >> 8);
    pseudo[11] = (uint8_t)(tcp_len & 0xFF);

    uint32_t sum = 0;

    /* Sum pseudo-header as big-endian 16-bit words */
    for (int i = 0; i < 12; i += 2)
        sum += ((uint16_t)pseudo[i] << 8) | pseudo[i + 1];

    /* Sum TCP segment as big-endian 16-bit words */
    const uint8_t *d = (const uint8_t *)tcp_data;
    uint16_t i;
    for (i = 0; i + 1 < tcp_len; i += 2)
        sum += ((uint16_t)d[i] << 8) | d[i + 1];
    if (i < tcp_len)
        sum += (uint16_t)d[i] << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t result = (uint16_t)~sum;

    /* Return in network byte order — caller stores in packed struct */
    return htons(result);
}

static int tcp_send_packet(uint8_t flags, const void *data, uint16_t data_len) {
    static uint8_t pkt[1480];
    uint16_t hdr_len = 20;

    /* Add MSS option on SYN */
    if (flags & TCP_SYN) hdr_len = 24;

    /* Build IPv4 header */
    struct ipv4_header *ip = (struct ipv4_header *)pkt;
    uint16_t tcp_total = hdr_len + data_len;
    ip->ver_ihl    = 0x45;
    ip->tos        = 0;
    ip->total_len  = htons(20 + tcp_total);
    ip->id         = htons(ip_id_counter++);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = 6;
    ip->checksum   = 0;
    ip->src_ip     = config.ip;
    ip->dst_ip     = tcp.remote_ip;
    ip->checksum   = checksum(ip, 20);

    /* Build TCP header */
    struct tcp_header *th = (struct tcp_header *)(pkt + 20);
    th->src_port = htons(tcp.local_port);
    th->dst_port = htons(tcp.remote_port);
    th->seq_num = htonl(tcp.seq);
    th->ack_num = htonl(tcp.ack);
    th->data_offset = (uint8_t)((hdr_len / 4) << 4);
    th->flags = flags;
    th->window = htons(16384);
    th->checksum = 0;
    th->urgent_ptr = 0;

    if (flags & TCP_SYN) {
        /* MSS option: kind=2, len=4, value=1460 */
        pkt[40] = 2; pkt[41] = 4;
        pkt[42] = (uint8_t)(1460 >> 8);
        pkt[43] = (uint8_t)(1460 & 0xFF);
    }

    if (data && data_len > 0)
        mem_copy(pkt + 20 + hdr_len, data, data_len);

    th->checksum = tcp_checksum(pkt + 20, tcp_total, config.ip, tcp.remote_ip);

    /* Send directly via Ethernet using cached MAC — avoids blocking ARP in ISR context */
    return net_send_ethernet(tcp.remote_mac, ETH_TYPE_IPV4, pkt, 20 + tcp_total);
}

int net_tcp_connect(uint32_t ip, uint16_t port) {
    mem_set(&tcp, 0, sizeof(tcp));
    tcp.state = TCP_STATE_CLOSED;
    tcp.remote_ip = ip;
    tcp.remote_port = port;
    tcp.local_port = 49152 + (uint16_t)(timer_get_ticks() & 0x3FFF);
    tcp.seq = timer_get_ticks() * 7919 + 12345;
    tcp.initial_seq = tcp.seq;
    tcp.rx_len = 0;
    tcp.rx_fin = false;

    /* Resolve next-hop MAC now (in user context, blocking ARP is safe) */
    uint32_t next_hop;
    if (config.subnet != 0 && (ip & config.subnet) == (config.ip & config.subnet))
        next_hop = ip;
    else
        next_hop = config.gateway;
    if (!net_arp_lookup(next_hop, tcp.remote_mac)) {
        net_dbg("TCP: ARP fail\n");
        return -1;
    }

    /* Send SYN */
    tcp.state = TCP_STATE_SYN_SENT;
    tcp_send_packet(TCP_SYN, NULL, 0);
    tcp.seq++;  /* SYN consumes one sequence number */

    /* Wait for ESTABLISHED */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 500) {
        if (tcp.state == TCP_STATE_ESTABLISHED) return 0;
        if (tcp.state == TCP_STATE_CLOSED) return -1;
        __asm__ __volatile__("hlt");
    }

    tcp.state = TCP_STATE_CLOSED;
    return -1;
}

int net_tcp_send(const void *data, uint16_t len) {
    if (tcp.state != TCP_STATE_ESTABLISHED) return -1;

    /* Send in segments */
    const uint8_t *p = (const uint8_t *)data;
    while (len > 0) {
        uint16_t seg = len > 1400 ? 1400 : len;
        tcp_send_packet(TCP_ACK | TCP_PSH, p, seg);
        tcp.seq += seg;
        p += seg;
        len -= seg;
    }
    return 0;
}

int net_tcp_receive(void *buf, uint16_t max, uint32_t timeout_ticks) {
    uint32_t start = timer_get_ticks();

    while (timer_get_ticks() - start < timeout_ticks) {
        if (tcp.rx_len > 0) {
            uint16_t copy = tcp.rx_len > max ? max : tcp.rx_len;
            mem_copy(buf, tcp.rx_buf, copy);
            /* Shift remaining data */
            if (copy < tcp.rx_len)
                mem_copy(tcp.rx_buf, tcp.rx_buf + copy, tcp.rx_len - copy);
            tcp.rx_len -= copy;
            return (int)copy;
        }
        if (tcp.rx_fin || tcp.state == TCP_STATE_CLOSED) {
            return 0;  /* Connection closed */
        }
        __asm__ __volatile__("hlt");
    }
    return tcp.rx_len > 0 ? 0 : -1;  /* Timeout */
}

void net_tcp_close(void) {
    if (tcp.state == TCP_STATE_ESTABLISHED) {
        tcp.state = TCP_STATE_FIN_WAIT1;
        tcp_send_packet(TCP_FIN | TCP_ACK, NULL, 0);
        tcp.seq++;
    }
    /* Wait briefly for FIN exchange */
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 100 && tcp.state != TCP_STATE_CLOSED) {
        __asm__ __volatile__("hlt");
    }
    tcp.state = TCP_STATE_CLOSED;
}

static void net_handle_tcp(uint32_t src_ip, const void *data, uint16_t len) {
    if (len < 20) return;
    const struct tcp_header *th = (const struct tcp_header *)data;

    uint16_t src_port = ntohs(th->src_port);
    uint16_t dst_port = ntohs(th->dst_port);

    /* Must match our connection */
    if (src_ip != tcp.remote_ip || src_port != tcp.remote_port ||
        dst_port != tcp.local_port) return;

    uint32_t seq = ntohl(th->seq_num);
    uint32_t ack = ntohl(th->ack_num);
    uint8_t  flags = th->flags;
    uint8_t  hdr_len = (th->data_offset >> 4) * 4;
    uint16_t payload_len = len - hdr_len;
    const uint8_t *payload = (const uint8_t *)data + hdr_len;

    if (flags & TCP_RST) {
        tcp.state = TCP_STATE_CLOSED;
        return;
    }

    switch (tcp.state) {
    case TCP_STATE_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            tcp.ack = seq + 1;
            tcp.state = TCP_STATE_ESTABLISHED;
            tcp_send_packet(TCP_ACK, NULL, 0);
        }
        break;

    case TCP_STATE_ESTABLISHED:
        /* Handle incoming data */
        if (payload_len > 0 && seq == tcp.ack) {
            uint16_t space = (uint16_t)(sizeof(tcp.rx_buf) - tcp.rx_len);
            uint16_t copy = payload_len > space ? space : payload_len;
            if (copy > 0) {
                mem_copy(tcp.rx_buf + tcp.rx_len, payload, copy);
                tcp.rx_len += copy;
            }
            tcp.ack = seq + payload_len;
            tcp_send_packet(TCP_ACK, NULL, 0);
        }
        /* Handle FIN */
        if (flags & TCP_FIN) {
            tcp.ack = seq + payload_len + 1;
            tcp_send_packet(TCP_ACK, NULL, 0);
            tcp.rx_fin = true;
            tcp.state = TCP_STATE_CLOSED;
        }
        break;

    case TCP_STATE_FIN_WAIT1:
        if (flags & TCP_ACK) {
            if (flags & TCP_FIN) {
                tcp.ack = seq + 1;
                tcp_send_packet(TCP_ACK, NULL, 0);
                tcp.state = TCP_STATE_CLOSED;
            } else {
                tcp.state = TCP_STATE_FIN_WAIT2;
            }
        }
        /* Also handle any data in FIN_WAIT1 */
        if (payload_len > 0 && seq == tcp.ack) {
            uint16_t space = (uint16_t)(sizeof(tcp.rx_buf) - tcp.rx_len);
            uint16_t copy = payload_len > space ? space : payload_len;
            if (copy > 0) {
                mem_copy(tcp.rx_buf + tcp.rx_len, payload, copy);
                tcp.rx_len += copy;
            }
            tcp.ack = seq + payload_len;
        }
        break;

    case TCP_STATE_FIN_WAIT2:
        if (flags & TCP_FIN) {
            tcp.ack = seq + payload_len + 1;
            tcp_send_packet(TCP_ACK, NULL, 0);
            tcp.state = TCP_STATE_CLOSED;
        }
        if (payload_len > 0 && seq == tcp.ack) {
            uint16_t space = (uint16_t)(sizeof(tcp.rx_buf) - tcp.rx_len);
            uint16_t copy = payload_len > space ? space : payload_len;
            if (copy > 0) {
                mem_copy(tcp.rx_buf + tcp.rx_len, payload, copy);
                tcp.rx_len += copy;
            }
            tcp.ack = seq + payload_len;
            tcp_send_packet(TCP_ACK, NULL, 0);
        }
        break;
    }
    (void)ack;
}

/* ---- HTTP ---- */

static int parse_url(const char *url, char *host, int host_max,
                     uint16_t *port, char *path, int path_max) {
    *port = 80;
    path[0] = '/'; path[1] = '\0';

    /* Skip "http://" */
    if (str_starts_with(url, "http://")) url += 7;

    /* Extract host (up to '/', ':', or end) */
    int hi = 0;
    while (*url && *url != '/' && *url != ':' && hi < host_max - 1)
        host[hi++] = *url++;
    host[hi] = '\0';

    /* Optional port */
    if (*url == ':') {
        url++;
        *port = 0;
        while (*url >= '0' && *url <= '9') {
            *port = *port * 10 + (*url - '0');
            url++;
        }
    }

    /* Path */
    if (*url == '/') {
        int pi = 0;
        while (*url && pi < path_max - 1)
            path[pi++] = *url++;
        path[pi] = '\0';
    }

    return hi > 0 ? 0 : -1;
}

int net_http_get(const char *url, char *buf, int max_size) {
    static char host[128];
    static char path[256];
    uint16_t port;

    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) < 0)
        return -1;

    net_dbg("HTTP: host=");
    net_dbg(host);
    net_dbg(" path=");
    net_dbg(path);
    net_dbg("\n");

    /* DNS resolve */
    uint32_t ip;
    if (!net_dns_resolve(host, &ip)) {
        net_dbg("HTTP: DNS fail\n");
        return -2;  /* DNS failure */
    }

    net_dbg("HTTP: resolved IP ok\n");

    /* TCP connect */
    if (net_tcp_connect(ip, port) < 0) {
        net_dbg("HTTP: connect fail\n");
        return -3;  /* TCP connect failure */
    }

    net_dbg("HTTP: connected\n");

    /* Build HTTP request */
    static char req[512];
    int ri = 0;
    /* "GET /path HTTP/1.0\r\n" */
    const char *g = "GET ";
    while (*g) req[ri++] = *g++;
    const char *pp = path;
    while (*pp) req[ri++] = *pp++;
    const char *v = " HTTP/1.0\r\nHost: ";
    while (*v) req[ri++] = *v++;
    const char *hh = host;
    while (*hh) req[ri++] = *hh++;
    const char *cl = "\r\nConnection: close\r\nUser-Agent: AI_OS/0.3\r\n\r\n";
    while (*cl) req[ri++] = *cl++;
    req[ri] = '\0';

    net_tcp_send(req, (uint16_t)ri);

    /* Receive response */
    int total = 0;
    int body_start = -1;
    static uint8_t tmp[1024];

    while (total < max_size - 1) {
        int n = net_tcp_receive(tmp, sizeof(tmp), 500);
        if (n <= 0) break;

        for (int i = 0; i < n && total < max_size - 1; i++) {
            buf[total++] = (char)tmp[i];
        }
    }
    buf[total] = '\0';

    net_tcp_close();

    /* Find end of HTTP headers */
    for (int i = 0; i < total - 3; i++) {
        if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            body_start = i + 4;
            break;
        }
    }

    if (body_start < 0) {
        /* No headers found — return raw */
        return total;
    }

    /* Shift body to start of buffer */
    int body_len = total - body_start;
    mem_copy(buf, buf + body_start, (uint32_t)body_len);
    buf[body_len] = '\0';
    return body_len;
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
