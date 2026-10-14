#ifndef MINDONE_RXFILTER_FIRMWARE_H
#define MINDONE_RXFILTER_FIRMWARE_H

#include <stdint.h>

uint32_t rxfilter_firmware_packet(const uint8_t* descriptor);
uint32_t rxfilter_firmware_rx(const uint8_t* descriptor, uint32_t broadcast);
uint32_t rxfilter_firmware_command(uint8_t* command);
uint32_t rxfilter_firmware_legacy_reply(uint8_t* command, uint32_t status);
struct rxfilter_firmware_buffer {
    uint32_t type;
    uint32_t address;
    uint16_t token;
    uint16_t reserved;
};

_Static_assert(sizeof(struct rxfilter_firmware_buffer) == 12, "Invalid buffer ABI");

#ifdef RXFILTER_FIRMWARE_TEST
void* rxfilter_firmware_test_address(uint32_t address);
uint32_t rxfilter_firmware_test_allocate(uint32_t size, struct rxfilter_firmware_buffer* buffer);
void rxfilter_firmware_test_prepare(uint32_t address, uint32_t size);
uint32_t rxfilter_firmware_test_channel(void);
void rxfilter_firmware_test_send(uint32_t zero, uint32_t channel, uint32_t reserved, uint32_t token);
void rxfilter_firmware_test_reset(uint32_t bss);
uint32_t rxfilter_firmware_test_seek(uint32_t bss, const uint8_t* mac);
void rxfilter_firmware_test_clear(void);
void rxfilter_firmware_test_program(uint32_t slot, uint32_t reserved, const uint8_t* mac);
uint32_t rxfilter_firmware_test_reconfigure(void);
void rxfilter_firmware_test_assert(uint32_t file, uint32_t line);
#endif

#endif
