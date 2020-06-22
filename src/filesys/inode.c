#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/buffer_cache.h"
#include <stdio.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_BLOCK_ENTRIES 124 // 125 - 2
#define INDIRECT_BLOCK_ENTRIES BLOCK_SECTOR_SIZE / sizeof(block_sector_t)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    //uint32_t unused[125];               /* Not used. */

    uint32_t is_dir;                        /*0 for file, 1 for dir*/

    /* Data containing disk sector */
    block_sector_t direct_map_table[DIRECT_BLOCK_ENTRIES];  
    /* Indirect block which contains data blocks */
    block_sector_t indirect_block_sec;
    /* Double indirect block which contains indirect blocks */
    block_sector_t double_indirect_block_sec;
  };

/* The way inode points disk block */
enum direct_t
{
  NORMAL_DIRECT,      /* Inode save disk block num */
  INDIRECT,           /* Access by one index block */
  DOUBLE_INDIRECT,    /* Access by two index block */
  OUT_LIMIT           /* Wrong file offset */
};

/* Save way to access block addr, offset of index block */
struct sector_location
  {
    uint8_t directness;  /* Way to access disk block - from direct_t */
    int index1;           /* Entry offset for first index block */
    int index2;           /* Entry offset for second index block */
  };

/* Index block structrue */
struct inode_indirect_block
  {
    block_sector_t map_table[INDIRECT_BLOCK_ENTRIES]; /* not assigned: -1 */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    //struct inode_disk data;             /* Inode content. */
    struct lock extend_lock;            /* Semaphore lock */
  };

/* Newly defined static func on pj4. */
static bool get_disk_inode (const struct inode *inode, struct inode_disk *disk_inode);
static bool put_disk_inode (const struct inode *inode, struct inode_disk *disk_inode);
static void locate_byte (off_t pos, struct sector_location *sec_loc);
static inline off_t map_table_offset (int index);
static bool register_sector (struct inode_disk *disk_inode,\
 block_sector_t new_sector, struct sector_location sec_loc);
static bool inode_update_file_length (struct inode_disk* disk_inode,\
 off_t start_pos, off_t end_pos);
static void free_inode_sectors (struct inode_disk *disk_inode);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode_disk *disk_inode, off_t pos) 
{
  ASSERT (disk_inode != NULL);
  
  block_sector_t result_sec, second_sec;

  if (pos < disk_inode->length)
  {
    struct inode_indirect_block *ind_block, *d_ind_block;
    struct sector_location sec_loc;
    locate_byte (pos, &sec_loc);
    
    switch (sec_loc.directness)
    {
    case NORMAL_DIRECT:
      /* code */
      result_sec = disk_inode->direct_map_table[sec_loc.index1];
      break;
    case INDIRECT:
      /* code */
      result_sec = disk_inode->indirect_block_sec;
      if (result_sec == (block_sector_t) -1)
        return -1;
      ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
      if (ind_block == NULL)
        return -1;
      if (!bc_read (result_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0))
        return -1;
      result_sec = ind_block->map_table[sec_loc.index1];
      free (ind_block);
      break;
    case DOUBLE_INDIRECT:
      /* code */
      second_sec = disk_inode->double_indirect_block_sec;
      if (second_sec == (block_sector_t) -1)
        return -1;
      d_ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
      if (d_ind_block == NULL)
        return -1;
      if (!bc_read (second_sec, d_ind_block, 0, BLOCK_SECTOR_SIZE, 0))
        return -1;
      result_sec = d_ind_block->map_table[sec_loc.index2];
      if (result_sec == (block_sector_t) -1)
        return -1;
      ind_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
      if (ind_block == NULL)
        return -1;
      if (!bc_read (result_sec, ind_block, 0, BLOCK_SECTOR_SIZE, 0))
        return -1;
      result_sec = ind_block->map_table[sec_loc.index1];
      free (d_ind_block);
      free (ind_block);
      break;

    default:
      result_sec = -1;
      break;
    }
  }
  else
    result_sec = -1;
  return result_sec;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint32_t is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      memset (disk_inode, -1, BLOCK_SECTOR_SIZE);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;
      if (length > 0)
      {
        if (!inode_update_file_length(disk_inode, 0, length))
        {
          free (disk_inode);
          return false;
        }
      }
      /*
      if (free_map_allocate (sectors, &disk_inode->start)) 
        {
          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
        */
      if (!bc_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0))
      {
        free (disk_inode);
        return false;
      }
      free (disk_inode);
      success = true;
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  //block_read (fs_device, inode->sector, &inode->data);
  /* Init lock */
  lock_init(&inode->extend_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          struct inode_disk *disk_inode;
          disk_inode = (struct inode_disk *) malloc (BLOCK_SECTOR_SIZE);
          get_disk_inode (inode, disk_inode);
          free_inode_sectors(disk_inode);
          free_map_release (inode->sector, 1);
          free (disk_inode);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  //uint8_t *bounce = NULL;

  //lock_acquire (&inode->extend_lock);

  struct inode_disk *disk_inode;
  disk_inode = (struct inode_disk *) malloc (BLOCK_SECTOR_SIZE);
  get_disk_inode (inode, disk_inode);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read full sector directly into caller's buffer. */
      bc_read (sector_idx, buffer, bytes_read, chunk_size, sector_ofs);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  //free (bounce);
  free (disk_inode);
  //lock_release (&inode->extend_lock);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  //uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  lock_acquire (&inode->extend_lock);

  struct inode_disk *disk_inode;
  disk_inode = (struct inode_disk *) malloc (BLOCK_SECTOR_SIZE);
  if (disk_inode == NULL)
    return 0;
  get_disk_inode (inode, disk_inode);

  int old_length = disk_inode->length;
  int write_end = offset + size - 1;
  if (write_end > old_length - 1)
  {
    if (!inode_update_file_length (disk_inode, disk_inode->length, write_end))
      return 0;
  }
  lock_release (&inode->extend_lock);

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = disk_inode->length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_write (sector_idx, (void*)buffer, bytes_written, chunk_size, sector_ofs);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  put_disk_inode (inode, disk_inode);
  //free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t length;
  struct inode_disk *disk_inode;
  disk_inode = (struct inode_disk *) malloc (BLOCK_SECTOR_SIZE);
  get_disk_inode (inode, disk_inode);
  length = disk_inode->length;
  free (disk_inode);
  return length;
}

/* Read corresponding on disk inode from buffer cache and save on inode_disk. */
static bool get_disk_inode (const struct inode *inode, struct inode_disk *disk_inode)
{
  return bc_read (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
}

/* Write corresponding on disk inode from buffer cache and save on inode_disk. */
static bool put_disk_inode (const struct inode *inode, struct inode_disk *disk_inode)
{
  return bc_write (inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0);
}

/* Update directness and index in sector_location */
static void locate_byte (off_t pos, struct sector_location *sec_loc)
{
  off_t pos_sector = pos / BLOCK_SECTOR_SIZE;

  /* Case NORMAL_DIRECT */
  if (pos_sector < DIRECT_BLOCK_ENTRIES) 
  {
    sec_loc -> directness = NORMAL_DIRECT;
    sec_loc -> index1 = pos_sector; 
  }

  /* Case INDIRECT */
  else if (pos_sector < (off_t) (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES))
  {
    sec_loc -> directness = INDIRECT;
    off_t cur_pos_sector = pos_sector - DIRECT_BLOCK_ENTRIES;
    sec_loc -> index1 = cur_pos_sector;
  }

  /* Case DOUBLE_INDIRECT */
  else if (pos_sector < (off_t)\
   (DIRECT_BLOCK_ENTRIES + INDIRECT_BLOCK_ENTRIES * (INDIRECT_BLOCK_ENTRIES + 1)))
  {
    sec_loc -> directness = DOUBLE_INDIRECT;
    off_t cur_pos_sector = pos_sector - DIRECT_BLOCK_ENTRIES - INDIRECT_BLOCK_ENTRIES;
    sec_loc -> index2 = cur_pos_sector / INDIRECT_BLOCK_ENTRIES;
    sec_loc -> index1 = cur_pos_sector % INDIRECT_BLOCK_ENTRIES;
  }

  /* Case OUT_LIMIT */
  else
    sec_loc -> directness = OUT_LIMIT;  
}

/* offset index to byte - how does this function work w/o directness? */
static inline off_t map_table_offset (int index)
{
  return index * BLOCK_SECTOR_SIZE;
}

/* Update new disk block number to inode_disk */
static bool register_sector (struct inode_disk *disk_inode,\
 block_sector_t new_sector, struct sector_location sec_loc)
{
  struct inode_indirect_block *new_block, *second_block;
  block_sector_t *indirect_block, *d_indirect_block;
  
  switch (sec_loc.directness)
  {
  case NORMAL_DIRECT:
    /* code */
    disk_inode->direct_map_table[sec_loc.index1] = new_sector;
    break;
  case INDIRECT:
    /* code */
    new_block = malloc (BLOCK_SECTOR_SIZE);
    if (new_block == NULL)
      return false;
    indirect_block = &(disk_inode -> indirect_block_sec);
    /* first allocation of disk block */
    if (*indirect_block == (block_sector_t) -1)
    {
      if (!free_map_allocate (1, indirect_block))
        return false;
      memset (new_block, -1, BLOCK_SECTOR_SIZE);
    }
    else
    {
      if (!bc_read (*indirect_block, new_block, 0, BLOCK_SECTOR_SIZE, 0))
        return false;
    }
    new_block->map_table[sec_loc.index1] = new_sector;
    if (!bc_write (*indirect_block, new_block, 0, BLOCK_SECTOR_SIZE, 0))
      return false;
    free (new_block);
    break;
  case DOUBLE_INDIRECT:
    /* code */
    second_block = malloc (BLOCK_SECTOR_SIZE);
    if (second_block == NULL)
      return false;
    d_indirect_block = &(disk_inode -> double_indirect_block_sec);
    /* first allocation of disk block */
    if (*d_indirect_block == (block_sector_t) -1)
    {
      if (!free_map_allocate (1, d_indirect_block))
        return false;
      memset (second_block, -1, BLOCK_SECTOR_SIZE);
    }
    else
    {
      if (!bc_read (*d_indirect_block, second_block, 0, BLOCK_SECTOR_SIZE, 0))
        return false;
    }
    indirect_block = &(second_block->map_table[sec_loc.index2]);
    
    new_block = malloc (BLOCK_SECTOR_SIZE);
    if (new_block == NULL)
      return false;
    /* first allocation of disk block */
    if (*indirect_block == (block_sector_t) -1)
    {
      if (!free_map_allocate (1, indirect_block))
        return false;
      memset (new_block, -1, BLOCK_SECTOR_SIZE);
      if (!bc_write (*d_indirect_block, second_block, 0, BLOCK_SECTOR_SIZE, 0))
        return false;
    }
    else
    {
      if (!bc_read (*indirect_block, new_block, 0, BLOCK_SECTOR_SIZE, 0))
        return false;
    }
    new_block->map_table[sec_loc.index1] = new_sector;
    if (!bc_write (*indirect_block, new_block, 0, BLOCK_SECTOR_SIZE, 0))
      return false;
    free (second_block);
    free (new_block);
    break;
  default:
    return false;
  }
  return true;
}

/* When file offset if larger than original file, 
  assign new disk blocks and update inode */
static bool inode_update_file_length (struct inode_disk* disk_inode,\
 off_t start_pos, off_t end_pos)
{
  if (start_pos > end_pos)
    return false;


  block_sector_t sector_idx;
  uint8_t **zeroes;
  zeroes = (uint8_t **) malloc (BLOCK_SECTOR_SIZE);
  memset (zeroes, 0, BLOCK_SECTOR_SIZE);

  struct sector_location sec_loc;

  off_t size, offset;
  size = end_pos - start_pos;
  offset = start_pos;
  while (size > 0)
  {
    sector_idx = byte_to_sector (disk_inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = end_pos - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;
  
    if (sector_idx != (block_sector_t) -1)
    {
      /* Nothing to do with - may happen on first loop */
      continue;
    }
    else
    {
      if (free_map_allocate(1, &sector_idx))
      {
        locate_byte (offset, &sec_loc);
        if (!register_sector (disk_inode, sector_idx, sec_loc))
        {
          free(zeroes);
          return false;
        }
      }
      else
      {
        free(zeroes);
        return false;
      }
      if (!bc_write (sector_idx, zeroes, 0, BLOCK_SECTOR_SIZE, 0))
      {
        free (zeroes);
        return false;
      }
    }
    size -= chunk_size;
    offset += chunk_size;
  }
  free(zeroes);
  return true;
}

/* Release corresponding indirect sector table */
static void free_indirect_release (block_sector_t indirect_sector_idx)
{
  int i;
  struct inode_indirect_block *release_block;
  release_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
  if(!bc_read(indirect_sector_idx, release_block, 0, BLOCK_SECTOR_SIZE, 0))
    return;
  i = 0;
  while (release_block->map_table[i] > 0)
  {
    free_map_release (release_block->map_table[i], 1);
    i++;
  }
  free (release_block);
  free_map_release (indirect_sector_idx, 1);
}

/* Free all disk blocks for file */
static void free_inode_sectors (struct inode_disk *disk_inode)
{
  block_sector_t i, d_idx;
  struct inode_indirect_block *d_release_block;
  /* Double indirect */
  d_idx = disk_inode -> double_indirect_block_sec;
  if (d_idx > 0)
  {
    d_release_block = (struct inode_indirect_block *) malloc (BLOCK_SECTOR_SIZE);
    if(!bc_read(d_idx, d_release_block, 0, BLOCK_SECTOR_SIZE, 0))
      return;
    i = 0;
    while (d_release_block->map_table[i] > 0)
    {
      free_indirect_release (d_release_block->map_table[i]);
    }
    free_map_release (d_idx, 1);
    free (d_release_block);
  }
  /* Indirect */
  if (disk_inode->indirect_block_sec > 0)
  {
    free_indirect_release (disk_inode->indirect_block_sec);
  }
  /* Direct */
  i = 0;
  while (disk_inode -> direct_map_table[i] > 0)
  {
    free_map_release(disk_inode->direct_map_table[i], 1);
    i++;
  }
}

bool inode_is_dir (const struct inode *inode) {
  bool result;
  /* inode_disk 자료구조를 메모리에 할당 */
  /* in-memory inode의 on-disk inode를 읽어 inode_disk에 저장 */
  /* on-disk inode의 is_dir을 result에 저장하여 반환 */
  struct inode_disk *inode_disk;
  if (inode->removed)
    return false;
  if (!get_disk_inode (inode, inode_disk))
    return false;
  result = inode_disk->is_dir;
  return result;
}