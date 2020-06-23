#ifndef FILESYS_BUFFER_CACHE_H
#define FILESYS_BUFFER_CACHE_H

#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/synch.h"

struct buffer_head{
  // Flag that tells if corresponding entry is dirty
  bool dirty;
  // Flag that tells if corresponding entry is used
  bool used;
  // disk sector address of corresponding entry
  block_sector_t disk_addr;
  // clock bit for clock algorithm
  bool clock;
  // lock variable
  struct lock lock;
  // data pointer to point buffer cache entry
  void *buffer;
};

void bc_init(void);
void bc_term(void);
struct buffer_head* bc_select_victim (void);
struct buffer_head* bc_lookup (block_sector_t sector);
void bc_flush_entry (struct buffer_head *p_flush_entry);
void bc_flush_all_entries (void);
bool bc_read (block_sector_t sector_idx, void *buffer, off_t bytes_read, int chunk_size, int sector_ofs);
bool bc_write (block_sector_t sector_idx, void *buffer, off_t bytes_written, int chunk_size, int sector_ofs);

#endif