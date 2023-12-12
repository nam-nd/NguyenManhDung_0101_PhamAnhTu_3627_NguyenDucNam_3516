#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h> // Filesys For strtok 
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

#include "userprog/process.h"
#include <user/syscall.h>
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/inode.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}
bool chdir(const char* dir);
bool mkdir(const char* dir);
bool readdir(int fd, char *name);
bool isdir(int fd);
int inumber(int fd);
/*
  Changes the current working directory of the process to dir, which may be 
  relative or absolute. Returns true if successful, false on failure. 
*/
bool chdir(const char* dir) { 
  if (!dir || strlen(dir) == 0)
  {
    return false;
  }
  if(!thread_current()->curr_dir
   || (dir_get_inode(thread_current()->curr_dir)) == ROOT_DIR_SECTOR){
    if(!strcmp(dir, "/") || !strcmp(dir, "..") || !strcmp(dir, ".")){
      return false;
    }
  }
  struct dir *directory = handle_rel_abs_dir(dir);
  if(!directory){
    return false;
  }
  dir_close(thread_current()->curr_dir);
  thread_current()->curr_dir = directory;
  return true;
}

/*
  Creates the directory named dir, which may be relative or absolute. 
  Returns true if successful, false on failure. Fails if dir already exists 
  or if any directory name in dir, besides the last, does not already exist. 
  That is, mkdir("/a/b/c") succeeds only if "/a/b" already exists and 
  "/a/b/c" does not. 
*/
bool mkdir(const char* dir) {
  if(strlen(dir) == 0){
    return false;
  }
  struct dir *target = handle_rel_abs_dir(dir);
  if(target){
    // Directory exists
    dir_close(target);
    return false;
  }
  dir_close(target);
  // Check that the previous directory exists
  int index = strlen(dir) - 1;
  while(index >= 0 && dir[index] != '/'){
    index--;
  }
  index++;
  // Create the directory in root or thread's current directory
  if(index == 1 || index == 0){
    block_sector_t new_dir = 0;
    if(!free_map_allocate (1, &new_dir)
     || !dir_create(new_dir, DIR_CREATE_CNST)){
      return false;
    }
    char *name = index == 1 ? dir + 1 : dir;
    if(index == 0 && thread_current()->curr_dir != NULL){
      if(!dir_add(thread_current()->curr_dir, name, new_dir)){
        return false;
      }
      struct inode *inode = NULL;
      if(!dir_lookup(thread_current()->curr_dir, name, &inode)){
        inode_close(inode);
        return false;
      }
      struct dir *child = dir_open(inode);
      if(!dir_add(child, ".", new_dir) || !dir_add(child, "..", 
        inode_get_inumber(dir_get_inode(thread_current()->curr_dir)))){
           dir_close(child);
           return false;
      }
      dir_close(child);
    }else{
      if(!dir_add(dir_open_root(), name, new_dir)){
        return false;
      }
      struct inode *inode = NULL;
      if(!dir_lookup(dir_open_root(), name, &inode)){
        inode_close(inode);
        return false;
      }
      struct dir *child = dir_open(inode);
      if(!dir_add(child, ".", new_dir)
       || !dir_add(child, "..", ROOT_DIR_SECTOR)){
           dir_close(child);
           return false;
      }
      dir_close(child);
    }
    return true;
  }
  
  char *prev_name = calloc(1, strlen(dir) + 1);
  strlcpy(prev_name, dir, index);
  target = handle_rel_abs_dir(prev_name);
  free(prev_name);
  if(!target){
    // Directory does not exist
    return false;
  }
  // Allocate into target
  block_sector_t new_dir = 0;
  if(!free_map_allocate (1, &new_dir)
   || !dir_create(new_dir, DIR_CREATE_CNST)){
    return false;
  }
  if(!dir_add(target, dir + index, new_dir)){
    return false;
  }
      struct inode *inode = NULL;
      if(!dir_lookup(target, dir + index, &inode)){
        inode_close(inode);
        return false;
      }
      struct dir *child = dir_open(inode);  
      if(!dir_add(child, ".", new_dir)
       || !dir_add(child, "..", inode_get_inumber(dir_get_inode(target)))){
           dir_close(child);
           return false;
      }
  dir_close(child);
  return true;
}
/*
  Reads a directory entry from file descriptor fd, which must represent 
  a directory. If successful, stores the null-terminated file name in name, 
  which must have room for READDIR_MAX_LEN + 1 bytes, and returns true. 
  If no entries are left in the directory, returns false.

  "." and ".." should not be returned by readdir.

  If the directory changes while it is open, then it is acceptable for some 
  entries not to be read at all or to be read multiple times. Otherwise, each 
  directory entry should be read once, in any order.

  READDIR_MAX_LEN is defined in "lib/user/syscall.h". If your file system 
  supports longer file names than the basic file system, you should increase 
  this value from the default of 14.
*/
bool readdir(int fd, char *name) {
  struct file * curr_file = thread_current()->fd_list[fd];
   // Error checking
  if(curr_file == NULL){
    return false;
  }

  struct dir * curr_dir = (struct dir *) curr_file;
  struct inode *curr_inode = dir_get_inode(curr_dir);
  // Error checking
  if(curr_inode == NULL){
    return false;
  }
  // Need to check if is subdir
  bool sub_d = inode_is_subdir(curr_inode);
  if(!sub_d){
    return false;
  }

  bool continue_readdir = dir_readdir(curr_dir, name);
  // Special cases "." directory, and ".." directory.
  while(continue_readdir == true){
    if((strcmp(name, ".") != 0) && strcmp(name, "..") != 0){
      break;
    }
    else{
      continue_readdir =  dir_readdir(curr_dir, name);
    }
  }
  return continue_readdir;
}

/*  
  Returns true if fd represents a directory, false if 
  it represents an ordinary file. 
*/
bool isdir(int fd) {
  struct file * curr_file = thread_current()->fd_list[fd];
  // Error checking
  if(curr_file == NULL){
    return -1;
  }
  struct inode * inode = file_get_inode(curr_file);
  bool is_dir = inode_is_subdir(inode);
  return is_dir;
}

/*
  Returns the inode number of the inode associated with fd, which may 
  represent an ordinary file or a directory.

  An inode number persistently identifies a file or directory. It is 
  unique during the file's existence. In Pintos, the sector number of 
  the inode is suitable for use as an inode number.
*/
int inumber(int fd) {
  struct file * curr_file = thread_current()->fd_list[fd];
  // Error checking
  if(curr_file == NULL){
    return -1;
  }
  struct inode * inode = file_get_inode(curr_file);
  // Error checking
  int inum = -1;
  if(inode == NULL){
    return inum;
  }
  else{
    inum =  inode_get_inumber(inode);
    return inum;
  }
}