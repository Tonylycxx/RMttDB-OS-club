#include <stdio.h>
#include "threads/thread.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "vm/growstack.h"
#include "vm/page.h"

/* The stack size cannot grow beyond 256K.*/
#define MAX_STACK_SIZE (PHYS_BASE - PGSIZE * 64)
  
/* If a user program page faults on an address that might be a stack access
   grow the stack by mapping in a frame. */
void
maybe_grow_stack (uint32_t *pd, const void *vaddr)
{
  struct page *page;
  void *upage = pg_round_down (vaddr);

  if (pagedir_get_pageinfo (pd, upage) == NULL
      && is_stack_access (vaddr))
    {
      page = create_page_without_param();
      if (page != NULL)
        {
          page_set_upage (page, pg_round_down (vaddr));
          page_set_pagedir (page, pd);
          page_set_type (page, PAGE_TYPE_ZERO);
          page_set_writable (page, WRITABLE_TO_SWAP);
          pagedir_set_pageinfo (pd, upage, page);
         }
    }
 }

/* Stack accesses happen 1 or 4 bytes below the current value of the stack
   pointer as items are pushed on the stack.  In certain cases, the fault 
   address lies above the stack pointer.  This occurs when the stack pointer
   is decremented to allocate more stack and a value is written into the 
   stack. */ 
bool
is_stack_access (const void *vaddr)
{
  void *esp = thread_current ()->user_esp;

  return (vaddr >= MAX_STACK_SIZE
    && ((esp - vaddr) == 8 || (esp - vaddr) == 32
        || vaddr >= esp));
}
