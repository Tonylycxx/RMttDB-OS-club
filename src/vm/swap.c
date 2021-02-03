#include <stdbool.h>
#include <debug.h>
#include <bitmap.h>
#include "threads/vaddr.h"
#include "vm/swap.h"

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static bool swap_map_allocate(block_sector_t *sector_page);
static void swap_map_release(block_sector_t sector);

struct block *swap_device;

static struct bitmap *swap_map;

void swap_init(void)
{
  ASSERT(PGSIZE % BLOCK_SECTOR_SIZE == 0);
  swap_device = block_get_role(BLOCK_SWAP);
  swap_map = bitmap_create(block_size(swap_device) / SECTORS_PER_PAGE);
  if (swap_map == NULL)
    PANIC("swap section creation failed! -- swap device is too large");
}

block_sector_t swap_write(void *kpage)
{
  int i;
  block_sector_t sector;

  if (!swap_map_allocate(&sector))
    PANIC("no swap space");

  for (i = 0; i < SECTORS_PER_PAGE; i++, sector++, kpage += BLOCK_SECTOR_SIZE)
    block_write(swap_device, sector, kpage);
  return sector - SECTORS_PER_PAGE;
}

void swap_read(block_sector_t sector, void *kpage)
{
  int i;

  for (i = 0; i < SECTORS_PER_PAGE; i++, sector++, kpage += BLOCK_SECTOR_SIZE)
    block_read(swap_device, sector, kpage);
  swap_release(sector - SECTORS_PER_PAGE);
}

void swap_release(block_sector_t sector)
{
  swap_map_release(sector);
}

static bool swap_map_allocate(block_sector_t *sector_page)
{
  block_sector_t sector = bitmap_scan_and_flip(swap_map, 0, 1, false);
  if(sector != BITMAP_ERROR)
    *sector_page = sector * SECTORS_PER_PAGE;
  return sector != BITMAP_ERROR;
}

static void swap_map_release(block_sector_t sector)
{
  ASSERT(bitmap_all (swap_map, sector / SECTORS_PER_PAGE, 1));
  bitmap_set_multiple (swap_map, sector / SECTORS_PER_PAGE, 1, false);
}