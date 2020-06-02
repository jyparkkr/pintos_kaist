#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);
struct vm_entry* check_address(void *addr, void* esp);
void check_valid_buffer (void* buffer, unsigned size, void* esp, bool to_write);
void check_valid_string (const void* str, void* esp);
void get_argument(void *esp, int *arg , int count);

/* filesystem lock */
void halt (void);
void exit (int status);
tid_t exec (const char *cmd_line);
int wait (tid_t tid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
mapid_t mmap (int fd, void *addr);
void munmap (mapid_t mapping);
void seek (int fd, unsigned position);
unsigned tell (int fd); 
void close (int fd);

#endif /* userprog/syscall.h */
