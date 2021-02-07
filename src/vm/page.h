#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "devices/block.h"
#include "filesys/off_t.h"
#include "threads/synch.h"

struct page
{
    void *addr;
    struct thread *page_thread;
    struct hash_elem page_elem;
    struct frame *page_frame;
    struct file *page_file;
    block_sector_t page_sector;
    off_t file_offset;
    off_t file_bytes;
    bool private;
    bool writeable;
};

void destroy_page(struct hash_elem *h_elem, void*);
void page_exit(void);
struct page *page_for_addr(const void *addr);
struct page *page_valid_addr(const void *addr);
bool page_in_frame(struct page *page);
bool do_page_fault(void *fault_addr);
bool release_page(struct page *page);
bool page_accessed(struct page *page);

struct page *allocate_page(void *vaddr, bool writeable);
void deallocate_page(void *vaddr);

bool lock_page(void *addr, bool is_write);
void unlock_page(void *addr);

hash_hash_func page_in_hash;
hash_less_func compare_page;

#endif