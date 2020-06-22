#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  bc_init();
  inode_init ();
  free_map_init ();

  if (format) 
    do_format();

  free_map_open();
  thread_current()->cur_dir = dir_open_root ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  bc_term();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  char* cp_name = name;
  char file_name[PATH_MAX_LEN + 1];
  struct dir *dir = parse_path(cp_name, file_name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size,0)
                  && dir_add (dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char* cp_name = name;
  char file_name[PATH_MAX_LEN + 1];
  struct dir *dir = parse_path (cp_name, file_name);
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, file_name, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  char* cp_name = name;
  char file_name[PATH_MAX_LEN + 1];
  struct dir *dir = parse_path (cp_name, file_name);

  struct inode *inode;
  dir_lookup (dir, file_name, &inode);

  struct dir *cur_dir = NULL;
  char temp[PATH_MAX_LEN + 1];

  if (!inode_is_dir (inode)){
    if(!dir)
      success = false;
    else if(!dir_remove (dir, file_name))
      success = false;
    dir_close (dir);
    success=true;
  }
  else if((cur_dir = dir_open (inode))){
    if(!dir_readdir (cur_dir, temp)){
      if(!dir)
        success = false;
      else if(!dir_remove (dir, file_name))
        success = false;
      dir_close (dir);
      success = true;
    }
    dir_close (cur_dir);
  }
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

struct dir* parse_path (char *path_name, char *file_name) {
  struct dir *dir;
  if (path_name == NULL || file_name == NULL)
    goto fail;
  if (strlen(path_name) == 0)
    return NULL;
  /* PATH_NAME의 절대/상대경로에 따른 디렉터리 정보 저장 (구현)*/
  char *token, *nextToken, *savePtr;
  token = strtok_r (path_name, "/", &savePtr);
  nextToken = strtok_r (NULL, "/", &savePtr);
  while (token != NULL && nextToken != NULL){
    struct inode *inode = NULL;
    /* dir에서 token이름의 파일을 검색하여 inode의 정보를 저장*/
    if (!dir_lookup (dir, token, &inode))
    {
      dir_close (dir);
      return NULL;
    }
    /* inode가 파일일 경우 NULL 반환 */
    if (!inode_is_dir (inode))
    {
      dir_close (dir);
      return NULL;
    }
    /* dir의 디렉터리 정보를 메모리에서 해지 */
    dir_close (dir);
    /* inode의 디렉터리 정보를 dir에 저장 */
    dir = dir_open (inode);

    /* token에 검색할 경로 이름 저장 */
    token = next_token;
    next_token = strtok_r (NULL, "/", &save_ptr);
  }
  /* token의 파일 이름을 file_name에 저장*/
  strlcpy (file_name, token, PATH_MAX_LEN);
  /* dir 정보 반환 */
  return dir;
}