#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"

void init_swap(void);
block_sector_t swap_write(void *kpage);
void swap_read(block_sector_t sector, void *kpage);
void swap_release(block_sector_t sector);

#endif