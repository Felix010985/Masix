/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel, Mayakoika Support layer
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <drivers/rtl8139.h>
#include <drivers/io.h>
#include <string.h>
#include <linux/types.h>

/* Утилиты порядка байт */
#define ntohs(x)  (uint16_t)(((uint16_t)(x) >> 8) | ((uint16_t)(x) << 8))
#define htons(x)  ntohs(x)

/* PCI и аппаратные константы RTL8139 */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define RTL8139_VENDOR      0x10EC
#define RTL8139_DEVICE      0x8139

#define RX_BUFFER_SIZE      8192
#define TX_BUFFER_SIZE      1536
#define TX_COUNT            4

#define ETH_ARP             0x0806
#define ETH_IPV4            0x0800
#define IP_UDP              17

/* Регистры RTL8139 */
#define RTL_REG_MAC0        0x00
#define RTL_REG_TXSTATUS0   0x10
#define RTL_REG_TXADDR0     0x20
#define RTL_REG_RXBUF       0x30
#define RTL_REG_CHIPCMD     0x37
#define RTL_REG_CAPR        0x38
#define RTL_REG_CBR         0x3A
#define RTL_REG_INTRMASK    0x3C
#define RTL_REG_INTRSTATUS  0x3E
#define RTL_REG_RXCONFIG    0x44
#define RTL_REG_CONFIG1     0x52

#pragma pack(push, 1)

typedef struct {
    uint8_t  dest[6];
    uint8_t  src[6];
    uint16_t type;
} eth_hdr_t;

typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
} arp_pkt_t;

typedef struct {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_offset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src_ip[4];
    uint8_t  dest_ip[4];
} ip_hdr_t;

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

/* Аппаратный заголовок пакета в кольцевом буфере */
typedef struct {
    uint16_t status;
    uint16_t length;
} rtl_hdr_t;

typedef union {
    uint8_t raw[TX_BUFFER_SIZE];
    struct {
        eth_hdr_t eth;
        union {
            arp_pkt_t arp;
            struct {
                ip_hdr_t ip;
                /* UDP заголовок может смещаться, если есть IP Options,
                *  поэтому здесь он только для разметки памяти по умолчанию */
                udp_hdr_t udp;
            };
        };
    };
} net_packet_t;

#pragma pack(pop)

static uint16_t nic_io;
static uint8_t  nic_mac[6];
static uint8_t  rx_buffer[RX_BUFFER_SIZE + 16 + 1500] __attribute__((aligned(16)));
static uint8_t  tx_buffers[TX_COUNT][TX_BUFFER_SIZE]   __attribute__((aligned(4)));
static uint16_t rx_offset;
static uint8_t  tx_index;
static int      nic_ready;

static uint32_t rtl8139_pci_read(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
    ((uint32_t)function << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static int rtl8139_find_device(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = rtl8139_pci_read((uint8_t)bus, slot, 0, 0);
            if ((id & 0xFFFF) != RTL8139_VENDOR || (id >> 16) != RTL8139_DEVICE) continue;

            uint32_t bar = rtl8139_pci_read((uint8_t)bus, slot, 0, 0x10);
            if (!(bar & 1)) continue;

            nic_io = (uint16_t)(bar & ~3U);
            uint32_t command = rtl8139_pci_read((uint8_t)bus, slot, 0, 4);
            outl(PCI_CONFIG_ADDRESS, 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | 4);
            outl(PCI_CONFIG_DATA, command | 5);
            return 1;
        }
    }
    return 0;
}

static uint16_t rtl8139_checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) {
        sum += ((uint16_t)data[i] << 8) | data[i + 1];
    }
    if (length & 1) sum += (uint16_t)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static int rtl8139_transmit(const uint8_t *frame, uint16_t length) {
    if (!nic_ready || length > TX_BUFFER_SIZE) return 0;

    uint8_t index = tx_index++ & (TX_COUNT - 1);

    memcpy(tx_buffers[index], frame, length);
    for (uint16_t i = length; i < 60; i++) tx_buffers[index][i] = 0;

    outl(nic_io + RTL_REG_TXADDR0 + index * 4, (uint32_t)tx_buffers[index]);
    outl(nic_io + RTL_REG_TXSTATUS0 + index * 4, length < 60 ? 60 : length);
    return 1;
}

static void rtl8139_send_arp_reply(const uint8_t *request) {
    net_packet_t pkt;
    const eth_hdr_t *req_eth = (const eth_hdr_t *)request;
    const arp_pkt_t *req_arp = (const arp_pkt_t *)(request + sizeof(eth_hdr_t));

    /* Ethernet */
    memcpy(pkt.eth.dest, req_eth->src, 6);
    memcpy(pkt.eth.src, nic_mac, 6);
    pkt.eth.type = htons(ETH_ARP);

    /* ARP Payload */
    pkt.arp.hw_type    = htons(1);
    pkt.arp.proto_type = htons(ETH_IPV4);
    pkt.arp.hw_len     = 6;
    pkt.arp.proto_len  = 4;
    pkt.arp.opcode     = htons(2); /* Reply */

    memcpy(pkt.arp.sender_mac, nic_mac, 6);
    pkt.arp.sender_ip[0] = 10; pkt.arp.sender_ip[1] = 0; pkt.arp.sender_ip[2] = 2; pkt.arp.sender_ip[3] = 15;

    memcpy(pkt.arp.target_mac, req_arp->sender_mac, 6);
    memcpy(pkt.arp.target_ip, req_arp->sender_ip, 4);

    rtl8139_transmit(pkt.raw, sizeof(eth_hdr_t) + sizeof(arp_pkt_t));
}

static void rtl8139_echo_udp(const uint8_t *frame, uint16_t length) {
    if (length < 42 || frame[23] != IP_UDP) return;

    uint32_t ip_offset = sizeof(eth_hdr_t);
    uint32_t udp_offset = ip_offset + ((frame[ip_offset] & 0x0F) * 4);
    uint16_t udp_length = ((uint16_t)frame[udp_offset + 4] << 8) | frame[udp_offset + 5];

    if (udp_offset + udp_length > length || udp_length < 8) return;

    net_packet_t reply;

    /* Копируем Ethernet и переставляем MAC-адреса */
    memcpy(reply.eth.dest, ((eth_hdr_t *)frame)->src, 6);
    memcpy(reply.eth.src, nic_mac, 6);
    reply.eth.type = htons(ETH_IPV4);

    /* Копируем исходный IP заголовок целиком */
    uint32_t ip_hdr_len = udp_offset - ip_offset;
    memcpy(reply.raw + ip_offset, frame + ip_offset, ip_hdr_len);

    /* Меняем местами Source IP и Destination IP */
    ip_hdr_t *rep_ip = (ip_hdr_t *)(reply.raw + ip_offset);
    const ip_hdr_t *in_ip = (const ip_hdr_t *)(frame + ip_offset);
    memcpy(rep_ip->dest_ip, in_ip->src_ip, 4);
    memcpy(rep_ip->src_ip, in_ip->dest_ip, 4);

    /* Пересчитываем чексумму IP */
    rep_ip->checksum = 0;
    uint16_t csum = rtl8139_checksum(reply.raw + ip_offset, ip_hdr_len);
    rep_ip->checksum = htons(csum);

    /* Обрабатываем UDP */
    udp_hdr_t *rep_udp = (udp_hdr_t *)(reply.raw + udp_offset);
    const udp_hdr_t *in_udp = (const udp_hdr_t *)(frame + udp_offset);

    memcpy(rep_udp, in_udp, 8);
    rep_udp->dest_port = in_udp->src_port;
    rep_udp->src_port  = in_udp->dest_port;
    rep_udp->checksum  = 0;

    /* Копируем Payload данных UDP */
    memcpy(reply.raw + udp_offset + 8, frame + udp_offset + 8, udp_length - 8);

    rtl8139_transmit(reply.raw, udp_offset + udp_length);
}

static void rtl8139_receive_frame(const uint8_t *frame, uint16_t length) {
    if (length < sizeof(eth_hdr_t)) return;

    uint16_t protocol = ((uint16_t)frame[12] << 8) | frame[13];

    if (protocol == ETH_ARP) {
        if (length < 42 || frame[20] != 0 || frame[21] != 1) return;
        /* Проверяем, что запрос идет именно к масиксовому IP (10.0.2.15) */
        if (frame[38] == 10 && frame[39] == 0 && frame[40] == 2 && frame[41] == 15) {
            rtl8139_send_arp_reply(frame);
        }
    }
    else if (protocol == ETH_IPV4) {
        rtl8139_echo_udp(frame, length);
    }
}

void rtl8139_init(void) {
    if (!rtl8139_find_device()) return;

    outb(nic_io + RTL_REG_CONFIG1, 0x00);
    outb(nic_io + RTL_REG_CHIPCMD, 0x10);
    while (inb(nic_io + RTL_REG_CHIPCMD) & 0x10) {}

    for (int i = 0; i < 6; i++) nic_mac[i] = inb(nic_io + RTL_REG_MAC0 + i);

    outl(nic_io + RTL_REG_RXBUF, (uint32_t)rx_buffer);
    outb(nic_io + RTL_REG_RXCONFIG, 0x0F);
    outb(nic_io + RTL_REG_INTRMASK, 0x05);
    outb(nic_io + RTL_REG_CHIPCMD, 0x0C);
    outl(nic_io + RTL_REG_RXBUF, (uint32_t)rx_buffer);

    rx_offset = 0;
    nic_ready = 1;
}

int rtl8139_available(void) {
    return nic_ready;
}

void rtl8139_poll(void) {
    if (!nic_ready || !(inb(nic_io + RTL_REG_CHIPCMD) & 0x01)) return;

    uint16_t status = inw(nic_io + RTL_REG_INTRSTATUS);
    if (!(status & 0x01)) return;

    uint16_t current = inb(nic_io + RTL_REG_CBR);
    while (rx_offset != current) {
        uint8_t *packet = rx_buffer + rx_offset;
        rtl_hdr_t *hdr = (rtl_hdr_t *)packet;

        uint16_t length = hdr->length;
        if (length >= 4 && length <= 1518) {
            rtl8139_receive_frame(packet + sizeof(rtl_hdr_t), length - 4);
        }

        rx_offset = (rx_offset + length + sizeof(rtl_hdr_t) + 3) & ~3;
        if (rx_offset >= RX_BUFFER_SIZE) rx_offset -= RX_BUFFER_SIZE;

        outw(nic_io + RTL_REG_CAPR, rx_offset - 16);
        current = inb(nic_io + RTL_REG_CBR);
    }
    outw(nic_io + RTL_REG_INTRSTATUS, 1);
}
