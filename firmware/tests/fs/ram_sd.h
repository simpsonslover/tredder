#ifndef RAM_SD_H
#define RAM_SD_H
#include <stdbool.h>
#include <stdint.h>

extern uint8_t *ram_img;
extern uint32_t ram_blocks;
extern bool ram_present, ram_readonly, ram_open;
extern unsigned long ram_reads, ram_writes;

void ram_sd_create(uint32_t blocks);

#endif
