#ifndef VM_SWAP_H
#define VM_SWAP_H 1

#include <stdbool.h>
#include <list.h>
#include "vm/page.h"

static struct lock swap_lock;
static struct block *swap_block;
struct list swap_list;

struct swap_item {
	bool flag;
	unsigned int index;
    struct list_elem elem;
};
void swap_init (void);
struct swap_item *get_swap_item_by_index(unsigned int);
void swap_in(struct page *);
bool swap_out(struct page *);

#endif /* vm/swap.h */
