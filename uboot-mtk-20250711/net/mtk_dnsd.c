// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * Minimal DNS server for MediaTek web failsafe.
 *
 * Resolves a single domain name to the device's IP address.
 * The domain is read from the environment variable "dnsd_domain".
 * If not set, it is automatically created with the fallback value "failsafe.lan".
 * The IP address is taken from the active U-Boot network address,
 * with fallbacks to the "ipaddr" environment variable and 192.168.1.1.
 *
 * Listens on UDP port 53 and responds to standard DNS A-record queries.
 */

#include <env.h>
#include <log.h>
#include <net.h>

#include "dns.h"
#include "net/mtk_dnsd.h"

static rxhand_f *prev_udp_handler;
static bool dnsd_running;

#define DNSD_FALLBACK_DOMAIN "failsafe.lan"
#define DNSD_FALLBACK_IP     "192.168.1.1"
#define DNSD_ENV_DOMAIN      "dnsd_domain"
#define DNSD_ENV_IPADDR      "ipaddr"
#define DNSD_TTL              300

/*
 * DNS header flag bits
 */
#define DNS_FLAG_QR	0x8000	/* query/response */
#define DNS_FLAG_OPCODE	0x7800	/* opcode mask */
#define DNS_FLAG_AA	0x0400	/* authoritative answer */
#define DNS_FLAG_RD	0x0100	/* recursion desired */

#define DNS_RCODE_NOERROR	0x0000
#define DNS_RCODE_NXDOMAIN	0x0003

/* DNS class: Internet */
#define DNS_CLASS_IN	0x0001

static struct in_addr dnsd_get_our_ip(void)
{
	char *ip_str;
	struct in_addr ip = net_ip;

	if (ip.s_addr)
		return ip;

	ip_str = env_get(DNSD_ENV_IPADDR);
	if (!ip_str || !*ip_str)
		ip_str = DNSD_FALLBACK_IP;

	return string_to_ip(ip_str);
}

/*
 * Parse a DNS name label-by-label to obtain the total octet length of the
 * raw encoded name in the query packet (including the terminating zero-length
 * label). Returns 0 on failure.
 */
static int dnsd_parse_name_len(const uchar *pkt, int pkt_len, int offset)
{
	int count = 0;
	int hopped = 0;

	while (hopped < 10) {
		if (offset < 0 || offset >= pkt_len)
			return 0;
		uint8_t label_len = pkt[offset];

		if (label_len == 0)
			return count + 1;

		if ((label_len & 0xC0) == 0xC0)
			return count + 2;

		count += label_len + 1;
		offset += label_len + 1;
		hopped++;
	}
	return 0;
}

/*
 * Check whether the DNS name in the query matches our configured domain.
 * Matching is case-insensitive.
 */
static bool dnsd_name_matches(const uchar *pkt, int pkt_len,
			      int offset, const char *domain)
{
	const char *d = domain;

	while (*d) {
		if (offset < 0 || offset >= pkt_len)
			return false;

		uint8_t label_len = pkt[offset];
		if (label_len == 0 || (label_len & 0xC0))
			return false;

		offset++;

		for (int i = 0; i < label_len; i++) {
			if (offset >= pkt_len)
				return false;

			char dc = *d++;
			if (dc >= 'A' && dc <= 'Z')
				dc += 'a' - 'A';
			char pc = pkt[offset];
			if (pc >= 'A' && pc <= 'Z')
				pc += 'a' - 'A';

			if (dc != pc)
				return false;
			offset++;
		}

		if (*d == '.')
			d++;
		else if (*d != '\0')
			return false;
	}
	if (offset >= pkt_len || pkt[offset] != 0)
		return false;

	return true;
}

static void dnsd_send_empty_response(uchar *pkt, int name_offset, int name_len,
				     uint16_t req_flags, uint16_t rcode,
				     struct ethernet_hdr *eth_hdr,
				     struct in_addr sip, unsigned int sport)
{
	uchar *pkt_hdr = (uchar *)net_tx_packet;
	int eth_size = net_set_ether(pkt_hdr, eth_hdr->et_src, PROT_IP);
	uchar *resp = pkt_hdr + eth_size + IP_UDP_HDR_SIZE;
	uint16_t flags;
	unsigned int pos;

	resp[0] = pkt[0];
	resp[1] = pkt[1];

	flags = DNS_FLAG_QR | DNS_FLAG_AA | (req_flags & DNS_FLAG_RD) | rcode;
	resp[2] = (flags >> 8) & 0xFF;
	resp[3] = flags & 0xFF;

	resp[4] = pkt[4];
	resp[5] = pkt[5];

	/* ANCOUNT = NSCOUNT = ARCOUNT = 0 */
	resp[6] = 0;
	resp[7] = 0;
	resp[8] = 0;
	resp[9] = 0;
	resp[10] = 0;
	resp[11] = 0;

	memcpy(resp + 12, pkt + name_offset, name_len + 4);
	pos = 12 + name_len + 4;

	net_set_udp_header(pkt_hdr + eth_size, sip,
			   sport, DNS_SERVICE_PORT, pos);
	net_send_packet(pkt_hdr, eth_size + IP_UDP_HDR_SIZE + pos);
}

/*
 * DNS query handler.
 * Forwards non-DNS packets to the previous UDP handler.
 */
static void dnsd_handler(uchar *pkt, unsigned int dport,
			 struct in_addr sip, unsigned int sport,
			 unsigned int len)
{
	struct ethernet_hdr *eth_hdr;
	uint16_t flags, qdcount;
	uint16_t qtype, qclass;
	char *domain_str;
	int name_offset, name_len;
	struct in_addr our_ip;
	unsigned int pos;
	int qtype_offset;

	if (!dnsd_running || dport != DNS_SERVICE_PORT || len < 17)
		goto forward;

	flags = ((uint16_t)pkt[2] << 8) | pkt[3];
	if ((flags & DNS_FLAG_QR) || (flags & DNS_FLAG_OPCODE) != 0)
		goto forward;

	qdcount = ((uint16_t)pkt[4] << 8) | pkt[5];
	if (qdcount != 1)
		goto forward;

	name_offset = 12;
	name_len = dnsd_parse_name_len(pkt, len, name_offset);
	if (name_len <= 0 || name_len > 256)
		goto forward;

	qtype_offset = name_offset + name_len;
	if (qtype_offset + 4 > len)
		goto forward;

	qtype = ((uint16_t)pkt[qtype_offset] << 8) | pkt[qtype_offset + 1];
	qclass = ((uint16_t)pkt[qtype_offset + 2] << 8) | pkt[qtype_offset + 3];

	if (qclass != DNS_CLASS_IN)
		goto forward;

	if (!net_tx_packet)
		goto forward;

	/*
	 * Safely retrieve the client MAC from the received frame header.
	 * In U-Boot, 'pkt' points to UDP payload. Subtracting IP and UDP headers
	 * gives us the Ethernet header.
	 */
	eth_hdr = (struct ethernet_hdr *)(pkt - IP_UDP_HDR_SIZE - ETHER_HDR_SIZE);

	domain_str = env_get(DNSD_ENV_DOMAIN);
	if (!domain_str || !*domain_str)
		domain_str = DNSD_FALLBACK_DOMAIN;

	if (!dnsd_name_matches(pkt, len, name_offset, domain_str)) {
		dnsd_send_empty_response(pkt, name_offset, name_len, flags,
					 DNS_RCODE_NXDOMAIN, eth_hdr, sip, sport);
		return;
	}

	/* Return NOERROR/NODATA for non-A queries such as AAAA and HTTPS. */
	if (qtype != DNS_A_RECORD) {
		dnsd_send_empty_response(pkt, name_offset, name_len, flags,
					 DNS_RCODE_NOERROR, eth_hdr, sip, sport);
		return;
	}

	our_ip = dnsd_get_our_ip();
	if (our_ip.s_addr == 0)
		goto forward;

	/* Build and send standard A-record response */
	{
		uchar *pkt_hdr = (uchar *)net_tx_packet;
		int eth_size = net_set_ether(pkt_hdr, eth_hdr->et_src, PROT_IP);
		uchar *resp = pkt_hdr + eth_size + IP_UDP_HDR_SIZE;
		uint32_t ttl = DNSD_TTL;

		resp[0] = pkt[0];
		resp[1] = pkt[1];

		flags = DNS_FLAG_QR | DNS_FLAG_AA | (flags & DNS_FLAG_RD);
		resp[2] = (flags >> 8) & 0xFF;
		resp[3] = flags & 0xFF;

		resp[4] = pkt[4];
		resp[5] = pkt[5];

		/* ANCOUNT = 1 */
		resp[6] = 0;
		resp[7] = 1;
		resp[8] = 0;
		resp[9] = 0;
		resp[10] = 0;
		resp[11] = 0;

		memcpy(resp + 12, pkt + name_offset, name_len + 4);
		pos = 12 + name_len + 4;

		/* Compression pointer to the query name (0xC00C) */
		resp[pos++] = 0xC0;
		resp[pos++] = 0x0C;

		/* Type: A = 0x0001 */
		resp[pos++] = 0x00;
		resp[pos++] = 0x01;

		/* Class: IN = 0x0001 */
		resp[pos++] = 0x00;
		resp[pos++] = 0x01;

		/* TTL */
		resp[pos++] = (ttl >> 24) & 0xFF;
		resp[pos++] = (ttl >> 16) & 0xFF;
		resp[pos++] = (ttl >> 8) & 0xFF;
		resp[pos++] = ttl & 0xFF;

		/* RDLENGTH: 4 */
		resp[pos++] = 0x00;
		resp[pos++] = 0x04;

		/* RDATA: Big-Endian IP address */
		memcpy(resp + pos, &our_ip.s_addr, 4);
		pos += 4;

		net_set_udp_header(pkt_hdr + eth_size, sip,
				   sport, DNS_SERVICE_PORT, pos);
		net_send_packet(pkt_hdr,
				eth_size + IP_UDP_HDR_SIZE + pos);
	}
	return;

forward:
	if (prev_udp_handler)
		prev_udp_handler(pkt, dport, sip, sport, len);
}

int mtk_dnsd_start(void)
{
	char *domain;
	struct in_addr ip;

	if (dnsd_running) {
		if (net_get_udp_handler() != dnsd_handler) {
			prev_udp_handler = net_get_udp_handler();
			net_set_udp_handler(dnsd_handler);
		}
		return 0;
	}

	domain = env_get(DNSD_ENV_DOMAIN);
	if (!domain || !*domain) {
		domain = DNSD_FALLBACK_DOMAIN;
		env_set(DNSD_ENV_DOMAIN, DNSD_FALLBACK_DOMAIN);
	}

	ip = dnsd_get_our_ip();
	if (ip.s_addr == 0)
		return 1;

	prev_udp_handler = net_get_udp_handler();
	net_set_udp_handler(dnsd_handler);

	dnsd_running = true;
	printf("dnsd: started, resolving '%s' -> %pI4\n", domain, &ip);
	return 0;
}

void mtk_dnsd_stop(void)
{
	if (!dnsd_running)
		return;

	if (net_get_udp_handler() == dnsd_handler)
		net_set_udp_handler(prev_udp_handler);
	prev_udp_handler = NULL;
	dnsd_running = false;
	debug("dnsd: stopped\n");
}

bool mtk_dnsd_is_running(void)
{
	return dnsd_running;
}
