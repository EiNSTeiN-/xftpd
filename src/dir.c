
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dir.h"
#include "asprintf.h"

char *dir_fullpath(const char *path)
{
#ifdef WIN32
  return _fullpath(NULL, path, 0);
#else
  char *resolved_path = malloc(MAXNAMLEN+1);
  
  if(!resolved_path)
    return NULL;
    
  return realpath(path, resolved_path);
#endif
}

struct dir_ctx *dir_open(const char *path, const char *filespec)
{
  struct dir_ctx *dir;
  dir = malloc(sizeof(struct dir_ctx));
  if(!dir)
    return NULL;
  
#ifdef WIN32
  dir->filespec = bprintf("%s/%s", path, filespec);
  if(!dir->filespec) {
    free(dir);
    return NULL;
  }
  
  dir->info = _findfirst(dir->filespec, &dir->entry);
  if(dir->info != -1)
    return dir;
#else
  dir->filespec = strdup(filespec);
  if(!dir->filespec) {
    free(dir);
    return NULL;
  }
  
  dir->info = opendir(path);
  if(dir->info) {
    dir->entry = readdir(dir->info);
    return dir;
  }
#endif
  
  free(dir->filespec);
  free(dir);
  return NULL;
}

int dir_next(struct dir_ctx *dir)
{
#ifdef WIN32
  if(_findnext(dir->info, &dir->entry))
    return 0;
  return 1;
#else
  do {
    dir->entry = readdir(dir->info);
  } while(dir->entry && strcasewildcmp(dir->filespec, dir_name(dir)));
  return (dir->entry != NULL);
#endif
}

int dir_close(struct dir_ctx *dir)
{
#ifdef WIN32
  int r = _findclose(dir->info);
#else
  int r = closedir(dir->info);
#endif
  free(dir->filespec);
  free(dir);
  return r;
}
