#include "vm/page.h"
#include <stdio.h>
#include <string.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

#define STACK_MAX (1024 * 1024)

void
destroy_page(struct hash_elem *h_elem, void *aux UNUSED)
{
    struct page *page = hash_entry(h_elem, struct page, page_elem);
    frame_lock(page);
    if(page->page_frame)
        frame_free(page->page_frame);
    free(page);
}

void
page_exit(void)
{
    struct hash *hash_table = thread_current()->pages;
    if(hash_table != NULL)
        hash_destroy(hash_table, destroy_page);
}

struct page *
page_for_addr(const void *addr)
{
    if(addr < PHYS_BASE)
    {
        struct page page;
        struct hash_elem *h_elem;

        page.addr = (void *)pg_round_down(addr);
        h_elem = hash_find (thread_current()->pages, &page.page_elem);
        if(h_elem != NULL)
            return hash_entry(h_elem, struct page, page_elem);
        if((page.addr > PHYS_BASE - STACK_MAX) && ((void *)thread_current()->user_esp - 32 < addr)) {
            return allocate_page(page.addr, true);
        }
    }
    return NULL;
}

struct page *
page_valid_addr(const void *addr)
{
    if(addr < PHYS_BASE)
    {
        struct page page;
        struct hash_elem *h_elem;

        page.addr = (void *)pg_round_down(addr);
        h_elem = hash_find (thread_current()->pages, &page.page_elem);
        if(h_elem != NULL)
            return hash_entry(h_elem, struct page, page_elem);
    }

    return NULL;
}

bool
page_in_frame(struct page *page)
{
    page->page_frame = alloc_frame(page);
    if(page->page_frame == NULL)
        return false;

    if(page->page_sector != (block_sector_t) -1)
        swap_in(page);
    else if(page->page_file != NULL)
    {
        off_t read_bytes = file_read_at(page->page_file, page->page_frame->address_start, page->file_bytes, page->file_offset);
        off_t zero_bytes = PGSIZE - read_bytes;
        memset(page->page_frame->address_start + read_bytes, 0, zero_bytes);
    }
    else
        memset (page->page_frame->address_start, 0, PGSIZE);
    return true;
}

bool
do_page_fault(void *fault_addr)
{
    struct page *page;
    if(thread_current ()->pages == NULL){
        return false;
    }

    page = page_for_addr(fault_addr);
    if (page == NULL) {
        return false;
    }
    frame_lock(page);
    if (page->page_frame == NULL){
        if (!page_in_frame(page)){
            return false;
        }
    }
    ASSERT (lock_held_by_current_thread (&page->page_frame->use_lock));
    bool ret = pagedir_set_page(thread_current ()->pagedir, page->addr, page->page_frame->address_start, page->writeable);
    frame_unlock (page->page_frame);

    return ret;
}

bool 
release_page(struct page *page)
{
    bool done = false;

    ASSERT(page->page_frame != NULL);
    ASSERT(lock_held_by_current_thread(&page->page_frame->use_lock));
    pagedir_clear_page(page->page_thread->pagedir, (void *)page->addr);
    bool is_dirty = pagedir_is_dirty(page->page_thread->pagedir, (const void *)page->addr);

    if(!is_dirty)
        done = true;
    if (page->page_file == NULL)
        done = swap_out(page);
    else if(is_dirty)
        if(page->private)
            done = swap_out(page);
        else
            done = file_write_at(page->page_file, (const void *)page->page_frame->address_start, page->file_bytes, page->file_offset);
    if(done)
        page->page_frame = NULL;

    return done;
}

bool
page_accessed(struct page *page)
{
    ASSERT(page->page_frame != NULL);
    ASSERT(lock_held_by_current_thread(&page->page_frame->use_lock));

    bool is_accessed = pagedir_is_accessed(page->page_thread->pagedir, page->addr);
    if(is_accessed)
        pagedir_set_accessed(page->page_thread->pagedir, page->addr, false);

    return is_accessed;
}

struct page *
allocate_page(void *vaddr, bool writeable)
{
    struct thread *thread = thread_current();
    struct page *page = malloc(sizeof(*page));
    if(page!=NULL)
    {
        page->addr = pg_round_down(vaddr);
        page->writeable = writeable;
        page->private = writeable;
        page->page_frame = NULL;
        page->page_sector = (block_sector_t)-1;
        page->page_file = NULL;
        page->file_offset = 0;
        page->file_bytes = 0;
        page->page_thread = thread_current();

        if(hash_insert(thread->pages, &page->page_elem) != NULL) {
            free(page);
            page = NULL;
        }
            
    }
    return page;
}

void
deallocate_page(void *vaddr)
{
    struct page *page = page_for_addr(vaddr);
    ASSERT(page != NULL);
    frame_lock(page);
    if (page->page_frame)
    {
        struct frame *frame = page->page_frame;
        if (page->page_file && !page->private)
            release_page(page);
        frame_free(frame);
    }
    hash_delete(thread_current()->pages, &page->page_elem);
    free (page);
}

unsigned
page_in_hash(const struct hash_elem *h_elem, void *aux UNUSED)
{
    struct page *page = hash_entry(h_elem, struct page, page_elem);
    return ((uintptr_t)page->addr) >> PGBITS;
}

bool
compare_page(const struct hash_elem *a, const struct hash_elem *b, void* aux UNUSED)
{
    const struct page *a_ = hash_entry(a, struct page, page_elem);
    const struct page *b_ = hash_entry(b, struct page, page_elem);

    return a_->addr < b_->addr;
}

bool
lock_page(void *addr, bool is_write)
{
    struct page *page = page_for_addr(addr);
    if(page == NULL || (!page->writeable && is_write))
        return false;
    frame_lock (page);
    if (page->page_frame == NULL)
        return (page_in_frame(page) && pagedir_set_page(thread_current()->pagedir, page->addr, page->page_frame->address_start, page->writeable));
    else
        return true;
}

void
unlock_page(void *addr)
{
    struct page *page = page_for_addr(addr);
    ASSERT (page != NULL);
    frame_unlock(page->page_frame);
}
