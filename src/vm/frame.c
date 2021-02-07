#include "vm/frame.h"
#include <stdio.h>
#include "vm/page.h"
#include "devices/timer.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/thread.h"

struct frame *frame_table;
struct lock alloc_lock;
uint32_t frame_num = 0 ; 
uint32_t hand = 0;
void  frame_table_init(void){
    lock_init(&alloc_lock);
    
    void * address_start;
    frame_table = (struct frame *) malloc (sizeof (struct frame) * init_ram_pages);
    if (frame_table == NULL){
        PANIC("the frames can't be allocated");
    }
    while((address_start = palloc_get_page(PAL_USER))!=NULL){
        struct frame* f = (struct frame *)(&frame_table[frame_num]);
        f->address_start = address_start;
        f->thread_belong_to = thread_current();
        lock_init(&f->use_lock);
        frame_num++;
    }
}


struct frame * try_alloc_frame(struct page * page){
    lock_acquire(&alloc_lock);
    for (uint32_t i = 0 ;i < frame_num;i++){
        struct frame * f = &frame_table[i];
        if(lock_try_acquire(&f->use_lock)){
            if(f->page == NULL){
                f->page = page;
                f->thread_belong_to = thread_current();
                lock_release(&alloc_lock);
                return f;
            }
            lock_release(&f->use_lock);
        }
    }
    for(uint32_t i = 0; i < 2*frame_num ; i++){
        struct frame *f = &frame_table[hand];
        if (++hand >= frame_num)
            hand = 0;
        if(lock_try_acquire(&f->use_lock)){
            if(f->page == NULL){
                f->page = page;
                lock_release(&alloc_lock);
                f->thread_belong_to = thread_current();
                return f;
            }
            if(page_accessed(f->page)){
                lock_release(&f->use_lock);
                continue;
            }
            if(!release_page(f->page)){
                lock_release(&f->use_lock);
                return NULL;
            }
            f->page = page;
            lock_release(&alloc_lock);
            f->thread_belong_to = thread_current();
            return f;
        }
    }
    lock_release(&alloc_lock);
    return NULL;
}


struct frame *
alloc_frame(struct page * page){
    uint32_t try = 0;
    for(try = 0;try<3;try++){
        struct frame * f = try_alloc_frame(page);
        if(f!=NULL){
            // ASSERT(f->use_lock->holder == thread_current());
            return f;
        }
        timer_msleep(1000);
    }
    return NULL;
}

void frame_lock(struct page* p){
    struct frame *f = p->page_frame;
    if(f==NULL){
        return ;
    }
    else{
        lock_acquire(&f->use_lock);
        if(f != p->page_frame){
            frame_unlock(f);
            // ASSERT (p->page_frame == NULL); 
        }
    }
}

void
frame_free(struct frame * f){
    // ASSERT(f->use_lock->holder == thread_current());
    f->page = NULL;
    frame_unlock(f);
}


void 
frame_unlock(struct frame *f){
    // ASSERT(f->use_lock->holder == thread_current());
    lock_release(&f->use_lock);
}