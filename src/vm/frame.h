#include <stdbool.h>
#include "threads/synch.h"

 struct frame
 {
    struct thread * thread_belong_to;
    void * address_start;
    struct lock use_lock;
    struct page * page;
 };

void  frame_table_init(void);
void frame_lock(struct page* p);
void frame_free(struct frame * f);
void  frame_unlock(struct frame *f);
struct frame * try_alloc_frame(struct page * page);
struct frame * alloc_frame(struct page * page);