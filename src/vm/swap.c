#include "vm/swap.h"
#include <stdio.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define PAGE_SECTORS (PGSIZE / BLOCK_SECTOR_SIZE)

void swap_init (void) {
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL) {
    printf ("block_get_role() error!\n");
    return;
  }
  lock_init(&swap_lock);
  list_init(&swap_list);
  for (int i=0; i<(block_size(swap_block)/PAGE_SECTORS); i++) {
    struct swap_item *item  = malloc(sizeof(struct swap_item));
		item->flag = true;
		item->index = i*PAGE_SECTORS;
		if(item == NULL) return;
		list_push_back(&swap_list, &item->elem);
  }
}

struct swap_item *get_swap_item_by_index(unsigned int index) {
	if(list_empty(&swap_list)) return NULL;
	struct swap_item * item;
	for(struct list_elem *e=list_begin(&swap_list); e!=list_end(&swap_list); e=list_next(e)) {
		item = list_entry(e, struct swap_item, elem);
		if(item->index == index)
		break;
	}
	return item;
}

void swap_in(struct page *p) {
  for (int i=0; i<PAGE_SECTORS; i++) {
    block_read(swap_block, p->page_sector+i, p->page_frame->address_start + i*BLOCK_SECTOR_SIZE);
  }
  struct swap_item * item = get_swap_item_by_index(p->page_sector);
  item->flag = true;
  p->page_sector = -1;
}


bool swap_out(struct page *p) {
  lock_acquire(&swap_lock);
	unsigned int index = -1;
	for (struct list_elem *e=list_begin(&swap_list); e!=list_end(&swap_list); e=list_next(e)) {
		struct swap_item *item = list_entry(e, struct swap_item, elem);
		if (item->flag == true) {
			item->flag = false;
			index = item->index;
			break;
    }
  }
  if (index == -1) {
		lock_release(&swap_lock);
		return false;
	}

  p->page_sector = index;
  for (int i=0; i<PAGE_SECTORS; i++) {
    block_write(swap_block, p->page_sector + i, (uint8_t *) p->page_frame->address_start + i * BLOCK_SECTOR_SIZE);
  }
  lock_release (&swap_lock);
  
  p->private = false;
  p->page_file = NULL;
  p->file_offset = 0;
  p->file_bytes = 0;

  return true;
}
