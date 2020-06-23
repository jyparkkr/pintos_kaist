#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);
void check_address(void *addr);
void get_argument(void *esp, int *arg , int count);

/* filesystem lock */
struct lock filesys_lock; 
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
void seek (int fd, unsigned position);
unsigned tell (int fd); 
void close (int fd);
bool sys_isdir (int fd);
bool sys_chdir (const char *dir);
bool sys_mkdir (const char *dir);
bool sys_readdir (int fd, char *name);
int sys_inumber (int fd);
#endif /* userprog/syscall.h */
