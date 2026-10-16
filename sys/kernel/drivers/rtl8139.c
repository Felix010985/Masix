/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <drivers/rtl8139.h>
#include <drivers/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define RTL8139_VENDOR     0x10EC
#define RTL8139_DEVICE     0x8139
#define RX_BUFFER_SIZE     8192
#define TX_BUFFER_SIZE     1536
#define TX_COUNT           4
#define ETH_ARP            0x0806
#define ETH_IPV4           0x0800
#define IP_UDP             17

static uint16_t nic_io;
static uint8_t nic_mac[6];
static uint8_t rx_buffer[RX_BUFFER_SIZE + 16 + 1500] __attribute__((aligned(16)));
static uint8_t tx_buffers[TX_COUNT][TX_BUFFER_SIZE] __attribute__((aligned(4)));
static uint16_t rx_offset;
static uint8_t tx_index;
static int nic_ready;

static uint32_t rtl8139_pci_read(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static int rtl8139_find_device(void) {
    for (uint16_t bus = 0; bus < 256; bus++) for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = rtl8139_pci_read((uint8_t)bus, slot, 0, 0);
        if ((id & 0xFFFF) != RTL8139_VENDOR || (id >> 16) != RTL8139_DEVICE) continue;
        uint32_t bar = rtl8139_pci_read((uint8_t)bus, slot, 0, 0x10);
        if ((bar & 1) == 0) continue;
        nic_io = (uint16_t)(bar & ~3U);
        uint32_t command = rtl8139_pci_read((uint8_t)bus, slot, 0, 4);
        outl(PCI_CONFIG_ADDRESS, 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | 4);
        outl(PCI_CONFIG_DATA, command | 5);
        uint32_t irq_mac = rtl8139_pci_read((uint8_t)bus, slot, 0, 0x3C);
        (void)irq_mac;
        return 1;
    }
    return 0;
}

static uint16_t rtl8139_checksum(const uint8_t *data, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) sum += ((uint16_t)data[i] << 8) | data[i + 1];
    if (length & 1) sum += (uint16_t)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static int rtl8139_transmit(const uint8_t *frame, uint16_t length) {
    if (!nic_ready || length > TX_BUFFER_SIZE) return 0;
    uint8_t index = tx_index++ & (TX_COUNT - 1);
    for (uint16_t i = 0; i < length; i++) tx_buffers[index][i] = frame[i];
    for (uint16_t i = length; i < 60; i++) tx_buffers[index][i] = 0;
    outl(nic_io + 0x20 + index * 4, (uint32_t)tx_buffers[index]);
    outl(nic_io + 0x10 + index * 4, length < 60 ? 60 : length);
    return 1;
}

static void rtl8139_send_arp_reply(const uint8_t *request) {
    uint8_t frame[42];
    for (int i = 0; i < 6; i++) { frame[i] = request[6 + i]; frame[6 + i] = nic_mac[i]; }
    frame[12] = 0x08; frame[13] = 0x06;
    frame[14] = 0; frame[15] = 1; frame[16] = 8; frame[17] = 0; frame[18] = 6; frame[19] = 4;
    frame[20] = 0; frame[21] = 2;
    for (int i = 0; i < 6; i++) { frame[22 + i] = nic_mac[i]; frame[32 + i] = request[22 + i]; }
    frame[28] = 10; frame[29] = 0; frame[30] = 2; frame[31] = 15;
    for (int i = 0; i < 4; i++) frame[38 + i] = request[28 + i];
    rtl8139_transmit(frame, sizeof(frame));
}

static void rtl8139_echo_udp(const uint8_t *frame, uint16_t length) {
    if (length < 42 || frame[23] != IP_UDP) return;
    uint32_t ip_header = 14;
    uint32_t udp_header = 14 + ((frame[ip_header] & 0x0F) * 4);
    uint16_t udp_length = ((uint16_t)frame[udp_header + 4] << 8) | frame[udp_header + 5];
    if (udp_header + udp_length > length || udp_length < 8) return;
    uint8_t reply[TX_BUFFER_SIZE];
    for (int i = 0; i < 6; i++) { reply[i] = frame[6 + i]; reply[6 + i] = nic_mac[i]; }
    reply[12] = 8; reply[13] = 0;
    for (uint32_t i = 0; i < 20; i++) reply[14 + i] = frame[14 + i];
    for (int i = 0; i < 4; i++) { reply[26 + i] = frame[30 + i]; reply[30 + i] = frame[26 + i]; }
    reply[24] = 0; reply[25] = 0; reply[24] = 0; reply[25] = 0;
    reply[24] = rtl8139_checksum(reply + 14, 20) >> 8; reply[25] = rtl8139_checksum(reply + 14, 20);
    for (int i = 0; i < 8; i++) reply[udp_header + i] = frame[udp_header + i];
    reply[udp_header] = frame[udp_header + 2]; reply[udp_header + 1] = frame[udp_header + 3];
    reply[udp_header + 2] = frame[udp_header]; reply[udp_header + 3] = frame[udp_header + 1];
    reply[udp_header + 6] = 0; reply[udp_header + 7] = 0;
    for (uint16_t i = udp_header + 8; i < udp_header + udp_length; i++) reply[i] = frame[i];
    rtl8139_transmit(reply, udp_header + udp_length);
}

static void rtl8139_receive_frame(const uint8_t *frame, uint16_t length) {
    if (length < 14) return;
    uint16_t protocol = ((uint16_t)frame[12] << 8) | frame[13];
    if (protocol == ETH_ARP && length >= 42 && frame[20] == 0 && frame[21] == 1 && frame[38] == 10 && frame[39] == 0 && frame[40] == 2 && frame[41] == 15) rtl8139_send_arp_reply(frame);
    else if (protocol == ETH_IPV4) rtl8139_echo_udp(frame, length);
}

void rtl8139_init(void) {
    if (!rtl8139_find_device()) return;
    outb(nic_io + 0x52, 0x00);
    outb(nic_io + 0x37, 0x10);
    while (inb(nic_io + 0x37) & 0x10) { }
    for (int i = 0; i < 6; i++) nic_mac[i] = inb(nic_io + i);
    outl(nic_io + 0x30, (uint32_t)rx_buffer);
    outb(nic_io + 0x44, 0x0F);
    outb(nic_io + 0x3C, 0x05);
    outb(nic_io + 0x37, 0x0C);
    outl(nic_io + 0x30, (uint32_t)rx_buffer);
    rx_offset = 0;
    nic_ready = 1;
}

int rtl8139_available(void) { return nic_ready; }

void rtl8139_poll(void) {
    if (!nic_ready || !(inb(nic_io + 0x37) & 1)) return;
    uint16_t status = inw(nic_io + 0x3E);
    if (!(status & 1)) return;
    uint16_t current = inb(nic_io + 0x3A);
    while (rx_offset != current) {
        uint8_t *packet = rx_buffer + rx_offset;
        uint16_t length = *(uint16_t*)(packet + 2);
        if (length >= 4 && length <= 1518) rtl8139_receive_frame(packet + 4, length - 4);
        rx_offset = (rx_offset + length + 4 + 3) & ~3;
        if (rx_offset >= RX_BUFFER_SIZE) rx_offset -= RX_BUFFER_SIZE;
        outw(nic_io + 0x38, rx_offset - 16);
        current = inb(nic_io + 0x3A);
    }
    outw(nic_io + 0x3E, 1);
}
