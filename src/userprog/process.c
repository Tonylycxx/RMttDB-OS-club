#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/timer.h"
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/frame.h"

// #define MY_DEBUG
#define PRO2W

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  struct thread* cur = thread_current();
  char token[50] = {'\0'};
  int num = 0;
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  for(int i = 0 ; i<strlen(file_name);i++){
    if(file_name[i] == ' ')
      break;
    token[num++] = file_name[i];
  }
  token[num] = '\0';
  file_name = token;
  file_name = token;

  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR){
    palloc_free_page (fn_copy);
  }

  cur->loading_success = 0;
  sema_down(&cur->sema_for_load); // when return the "is_waiting_load" must be 0

  if(cur->loading_success == 0){
    return -1;
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  struct thread *cur = thread_current();
  // printf("thread %s(%d) do start\n", cur->name, cur->tid);
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);


  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success){
    sema_up(&cur->father->sema_for_load);
    sys_exit(-1);
  }

  #ifdef PRO2W
  if(cur->father!=NULL){
    cur->father->loading_success = 1;
    sema_up(&cur->father->sema_for_load);
  }
  #endif

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  #ifdef PRO2W

  int child_exit_status = -1;
  struct list_elem *e;
  struct thread *cur = thread_current();
  struct thread *wait_child=NULL;
  struct thread_dying_status *wait_tds=NULL;

  for(e = list_begin(&cur->child_list); e != list_end(&cur->child_list); e=list_next(e)){
    struct thread *child_thread = list_entry(e, struct thread, child_elem);
    if(child_tid == child_thread->tid){
      wait_child = child_thread;
      break;
    }
  }
  for(e = list_begin(&cur->dying_c_status); e != list_end(&cur->dying_c_status); e = list_next(e)){
    struct thread_dying_status *child_tds = list_entry(e, struct thread_dying_status, elem);
    if(child_tds->tid == child_tid){
      wait_tds = child_tds;
      break;
    }
  }
  ASSERT(!(wait_child!=NULL&&wait_tds!=NULL));
  if(wait_tds!=NULL){
    child_exit_status = wait_tds->exit_code;
    list_remove(&wait_tds->elem);
    palloc_free_page(wait_tds);
    return child_exit_status;
  }
  else if(wait_child!=NULL){
    wait_child->is_being_waited = 1;
    sema_down(&wait_child->sema_for_wait);
    child_exit_status = cur->child_exit_code;
    cur->child_exit_code = 0;
    return child_exit_status;
  }
  return -1;
  #endif

  timer_sleep(400);
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  
  #ifdef PRO2W
  
  /* close all the files thread open*/
  for(int i=2; i<MAX_FILES_OPEN; i++){
    if(cur->open_files[i].is_using == 1){
      ASSERT(cur->open_files[i].open_fileptr != NULL);
      file_close(cur->open_files[i].open_fileptr);
    }
    else{
      ASSERT(cur->open_files[i].open_fileptr == NULL);
    }
    cur->open_files[i].is_using = 0;
    cur->open_files[i].open_fileptr = NULL;
  }
  if(cur->executing_file != NULL){
    file_close(cur->executing_file);
  }

  /* destroy all tds thread have*/
  while (!list_empty(&cur->dying_c_status))
  {
    struct list_elem *e = list_pop_front(&cur->dying_c_status);
    struct thread_dying_status *tds_temp = list_entry(e, struct thread_dying_status, elem);
    list_remove(&tds_temp->elem);
    palloc_free_page(tds_temp);
  }

  /* disconnect this thread and its child thread*/
  while (!list_empty(&cur->child_list)){
    struct list_elem *e = list_pop_front(&cur->child_list);
    struct thread *child_thread = list_entry(e, struct thread, child_elem);
    child_thread->father = NULL;
    list_remove(&child_thread->child_elem);
  }

  /* create a tds to record child exit status */
  if(cur->father!=NULL){
    if(cur->is_being_waited == 0){
      struct thread_dying_status *child_tds = palloc_get_page(0);
      child_tds->tid = cur->tid;
      child_tds->exit_code = cur->exit_code;
      list_push_back(&cur->father->dying_c_status, &child_tds->elem);
    }
  }

  struct thread *father = cur->father;
  if(father != NULL){
    cur->father = NULL;
    list_remove(&cur->child_elem);
  }
  if(father != NULL && cur->is_being_waited == 1){
    father->child_exit_code = cur->exit_code;
    sema_up(&cur->sema_for_wait);
  }

  if(lock_held_by_current_thread(&filesys_lock)){
    lock_release(&filesys_lock);
  }

  #endif
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp,char ** parameter,int length,int stack_part_length);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  //second question add start
  char * parameter[100];    
  char * token, *save_ptr;
  int num = 0;
  int stack_part_length = 0;
  for (token = strtok_r (file_name, " ", &save_ptr); token != NULL;token = strtok_r (NULL, " ", &save_ptr)){
    parameter[num] = token;
    stack_part_length += strlen(parameter[num])+1;
    num++;
  }
  if(stack_part_length%4 != 0){
    stack_part_length = (stack_part_length/4+1)*4;
  }
  stack_part_length += 4;
  //second question add end
  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  
  t->pages = malloc (sizeof *t->pages);
  if (t->pages == NULL)
    goto done;
  hash_init (t->pages, page_in_hash, compare_page, NULL);

  char *fn_copy = palloc_get_page(0);
  strlcpy(fn_copy, file_name, strlen(file_name)+1);
  char *temp = strtok_r(fn_copy, " ",&save_ptr);

  /* Open executable file. */
  // printf("file_name is %s\n",temp);
  lock_acquire(&filesys_lock);
  file = filesys_open (temp);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", temp);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp,parameter,num,stack_part_length))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

  file_deny_write(file);
  thread_current()->executing_file = file;

 done:
  /* We arrive here whether the load is successful or not. */
  palloc_free_page(fn_copy);
  lock_release(&filesys_lock);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct page *new_page = allocate_page(upage, writable);
      if (new_page == NULL)
        return false;
      // allocate page's file(if read_bytes > 0)
      if (page_read_bytes > 0) 
        {
          new_page->page_file = file;
          new_page->file_offset = ofs;
          new_page->file_bytes = page_read_bytes;
        }
      /* Get a page of memory. */
      // uint8_t *kpage = palloc_get_page (PAL_USER);
      

      /* Load this page. */
      // if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
      //   {
      //     palloc_free_page (kpage);
      //     return false; 
      //   }
      // memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      // if (!install_page (upage, kpage, writable)) 
      //   {
      //     palloc_free_page (kpage);
      //     return false; 
      //   }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp,char ** parameter,int length,int stack_part_length) 
{
  struct page *page = allocate_page(((uint8_t *) PHYS_BASE) - PGSIZE, true);
  if (page != NULL) 
    {
      page->page_frame = alloc_frame(page);
      page->private = false;
      frame_unlock(page->page_frame);

      *esp = PHYS_BASE;
      void ** esp_for_address = PHYS_BASE - (int)stack_part_length;
      int token_length = 0;
      for(int i = length - 1 ; i >= 0 ; i--){
        *((char **)esp) -= (strlen(parameter[i])+1);
        strlcpy((char*)*esp,parameter[i],strlen(parameter[i])+1);
        esp_for_address -= 1;
        *(esp_for_address) = *esp;
        token_length += strlen(parameter[i])+1;
      }
        int alignment = token_length%4;
      switch (alignment)
      {
        case 1:{
          *((uint8_t**)esp) -= 1;
          *(uint8_t*)(*esp) = 0;
          *((uint16_t**)esp) -= 1;
          *(uint16_t*)(*esp) = 0;
          break;
        }
        case 2:{
          *((uint16_t**)esp) -= 1;
          *(uint16_t*)(*esp) = 0;
          break;
        } 
        case 3:{
          *((uint8_t**)esp) -= 1;
          *(uint8_t*)(*esp) = 0;
          break;
        }
        default:
          break;
      }
      *esp -= 4;
      *(char**)(*esp) = 0;
      esp_for_address -= 1;
      *esp_for_address = esp_for_address+1;
      *esp = esp_for_address - 1;
      *(int*)(*esp) = length;
      *esp -= 4;
      *(void **)(*esp) = 0;
    }
  return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
