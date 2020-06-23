#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"
#include "threads/thread.h"

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
  char cp_name[PATH_MAX_LEN+1];
  strlcpy(cp_name,name,PATH_MAX_LEN+1);
  char file_name[PATH_MAX_LEN + 1];
  //printf("!!\n");
  struct dir *dir = parse_path(cp_name, file_name);
  //printf("!!22\n");
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size,0)
                  && dir_add (dir, file_name, inode_sector));
  //printf("success : %d\n",success);
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails.*/
struct file *
filesys_open (const char *name)
{
  char cp_name[PATH_MAX_LEN+1];
  strlcpy(cp_name,name,PATH_MAX_LEN+1);
  char file_name[PATH_MAX_LEN + 1];
  //struct dir *dir = parse_path (cp_name, file_name);
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
  bool success = false;
  char cp_name[PATH_MAX_LEN+1];
  strlcpy(cp_name,name,PATH_MAX_LEN+1);
  char file_name[PATH_MAX_LEN + 1];
  struct dir *dir = parse_path (cp_name, file_name);

  struct inode *inode;
  dir_lookup (dir, file_name, &inode);

  struct dir *cur_dir = NULL;
  char temp[PATH_MAX_LEN + 1];

  /* inode is file */
  if (!inode_is_dir (inode))
  {
    success = dir != NULL && dir_remove (dir, file_name);
    dir_close (dir);
  }
  /* inode is directory */
  else if((cur_dir = dir_open (inode)) && !dir_readdir (cur_dir, temp))
  {
    success = dir != NULL && dir_remove (dir, file_name);
    dir_close (cur_dir);
    dir_close (dir);
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

  struct dir *root_dir;
  root_dir = dir_open_root ();
  dir_add (root_dir, ".", ROOT_DIR_SECTOR);
  dir_add (root_dir, "..", ROOT_DIR_SECTOR);
  dir_close (root_dir);

  free_map_close ();
  printf ("done.\n");
}

/* Parse input in path_name to 
  path to path_name
  file to file_name. */
struct dir* parse_path (char *path_name, char *file_name) {
  struct dir *dir = NULL;
  if (path_name == NULL || file_name == NULL)
    return NULL;
  if (strlen(path_name) == 0)
    return NULL;
  /* To remain path_name */
  char path_tok[PATH_MAX_LEN+1];
  strlcpy (path_tok, path_name, PATH_MAX_LEN);
  if (path_tok[0] == '/')
    dir = dir_open_root ();
  else
    dir = dir_reopen (thread_current ()->cur_dir);

  if (!inode_is_dir (dir_get_inode (dir)))
    return NULL;
  char *token, *nextToken, *savePtr;
  token = strtok_r (path_tok, "/", &savePtr);
  nextToken = strtok_r (NULL, "/", &savePtr);
  if (token == NULL)
  {
    strlcpy (file_name, ".", PATH_MAX_LEN);
    return dir;
  }
  while (token != NULL && nextToken != NULL){
    struct inode *inode = NULL;
    if (!dir_lookup (dir, token, &inode))
    {
      dir_close (dir);
      return NULL;
    }
    if (!inode_is_dir (inode))
    {
      dir_close (dir);
      return NULL;
    }
    dir_close (dir);
    dir = dir_open (inode);

    token = nextToken;
    nextToken = strtok_r (NULL, "/", &savePtr);
  }
  strlcpy (file_name, token, PATH_MAX_LEN);
  return dir;
}

bool filesys_create_dir (const char *name)
{
  char cp_name[PATH_MAX_LEN+1];
  strlcpy(cp_name,name,PATH_MAX_LEN+1);
  char dir_name[PATH_MAX_LEN + 1];
  struct dir *dir = parse_path (cp_name, dir_name);

  block_sector_t inode_sector;

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, 16)
                  && dir_add (dir, dir_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  if (!success){
    dir_close (dir);
    return success;
  }
  struct dir *create_dir;
  create_dir = dir_open (inode_open (inode_sector));
  if (!create_dir){
    dir_close (dir);
    return false;
  }
  
  dir_add (create_dir, ".", inode_sector);
  dir_add (create_dir, "..", inode_get_inumber (dir_get_inode (dir)));
  dir_close (create_dir);

  dir_close (dir);
  return success;
}