#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H


#include "threads/synch.h"

struct lock filesys_lock;

void syscall_init (void);
void sys_exit (int);
void *is_valid_addr(const void *vaddr);

struct mmap{
    struct list_elem  elem;
    int mid;
    void * addr;
    int page_cnt;
    struct file * file;
};

#endif /* userprog/syscall.h */
