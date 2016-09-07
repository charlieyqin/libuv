/* Copyright libuv project contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include "os390-syscalls.h"
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <search.h>

#define CW_CONDVAR 32

#pragma linkage(BPX4CTW, OS)
#pragma linkage(BPX1CTW, OS)

#define PGTH_CURRENT  1
#define PGTH_LEN      26
#define PGTHAPATH     0x20
#pragma linkage(BPX4GTH, OS)
#pragma linkage(BPX1GTH, OS)

static int number_of_epolls;
struct _epoll_list* _global_epoll_list[MAX_EPOLL_INSTANCES];

int alphasort(const void *a, const void *b) {

  return strcoll( (*(const struct dirent **)a)->d_name,
                  (*(const struct dirent **)b)->d_name );
}

int scandir(const char *maindir, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **,
            const struct dirent **)) {
  struct dirent **nl = NULL;
  struct dirent *dirent;
  size_t count = 0;
  size_t allocated = 0;
  DIR *mdir;

  mdir = opendir(maindir);
  if (!mdir)
    return -1;

  while (1) {
    dirent = readdir(mdir);
    if (!dirent)
      break;
    if (!filter || filter(dirent)) {
      struct dirent *copy;
      copy = (struct dirent *)malloc(sizeof(*copy));
      if (!copy) {
        while (count) {
          dirent = nl[--count];
          free(dirent);
        }
        free(nl);
        closedir(mdir);
        errno = ENOMEM;
        return -1;
      }
      memcpy(copy, dirent, sizeof(*copy));

      nl = (struct dirent *)realloc(nl, count+1);
      nl[count++] = copy;
    }
  }

  qsort(nl, count, sizeof(struct dirent *),
      (int (*)(const void *, const void *))compar);

  closedir(mdir);

  *namelist = nl;
  return count;
}

static int isfdequal(const struct pollfd* a, const struct pollfd* b) {
  return a->fd == b->fd ? 0 : 1;
}

int epoll_create1(int flags)
{
  struct _epoll_list* p = (struct _epoll_list*)malloc(
                           sizeof(struct _epoll_list));

  memset(p, 0, sizeof(struct _epoll_list));
  int index = number_of_epolls++;
  _global_epoll_list[index] = p;

  if (uv_mutex_init(&p->lock)) {
    errno = ENOLCK;
    return -1;
  }

  p->size = 0;
  return index; 
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
  struct _epoll_list *lst = _global_epoll_list[epfd];

  if (op == EPOLL_CTL_DEL) {

    struct pollfd* found;
    struct pollfd pfd;

    pfd.fd = fd;
    uv_mutex_lock(&lst->lock);
    found = lfind(&pfd, &lst->items[0], &lst->size,
                  sizeof(struct pollfd), isfdequal);
    if (found != NULL)
      memcpy(found,
             found+1,
             (&lst->items[lst->size--] - found - 1) * sizeof(*found));
    uv_mutex_unlock(&lst->lock);

    if (found == NULL)
      return ENOENT;

  }
  else if(op == EPOLL_CTL_ADD)
  {
    struct pollfd* found;
    struct pollfd pfd;
    size_t before;
    size_t after;

    if (lst->size == MAX_ITEMS_PER_EPOLL - 1)
      return ENOMEM;

    pfd.fd = fd;
    pfd.events = event->events;
    uv_mutex_lock(&lst->lock);
    before = lst->size; 
    found = lsearch(&pfd, &lst->items[0], &lst->size,
                    sizeof(struct pollfd), isfdequal);
    after = lst->size; 
    uv_mutex_unlock(&lst->lock);

    if (found != NULL && before == after) {
      errno = EEXIST;
      return -1;
    }

  }
  else if(op == EPOLL_CTL_MOD)
  {
    int index;
    struct pollfd* found;
    struct pollfd pfd;

    pfd.fd = fd;
    uv_mutex_lock(&lst->lock);
    found = lfind(&pfd, &lst->items[0], &lst->size,
                  sizeof(struct pollfd), isfdequal);

    if (found != NULL)
      found->events = event->events;
      
    uv_mutex_unlock(&lst->lock);

    if (found == NULL) {
      errno = ENOENT;
      return -1;
    }
  }
  else 
  {
    abort();
  }
  return 0;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
  struct _epoll_list *lst = _global_epoll_list[epfd];

  unsigned int size = lst->size;

  struct pollfd *pfds = lst->items;
  int returnval = poll( pfds, size, timeout );
  if(returnval == -1)
    return returnval;

  int reventcount=0;
  int realsize = lst->size;
  for (int i = 0; i < realsize && i < maxevents; ++i)                     
  {
    struct epoll_event ev = { 0, 0 };
    ev.fd = pfds[i].fd;
    if(!pfds[i].revents)
      continue;

    if(pfds[i].revents & POLLRDNORM)
      ev.events = ev.events | POLLIN;

    if(pfds[i].revents & POLLWRNORM)
      ev.events = ev.events | POLLOUT;

    if(pfds[i].revents & POLLHUP)
      ev.events = ev.events | POLLHUP;

    pfds[i].revents = 0;
    events[reventcount++] = ev; 

  }

  return reventcount;
}

int epoll_file_close(int fd)
{
  for( int i = 0; i < number_of_epolls; ++i )
  {
    struct _epoll_list *lst = _global_epoll_list[i];
    struct pollfd* found;
    struct pollfd pfd;

    pfd.fd = fd;
    uv_mutex_lock(&lst->lock);
    found = lfind(&pfd, &lst->items[0], &lst->size,
                  sizeof(struct pollfd), isfdequal);
    if (found != NULL)
      memcpy(found,
             found+1,
             (&lst->items[lst->size--] - found - 1) * sizeof(*found));
    uv_mutex_unlock(&lst->lock);
  }

  return 0;
}

int getexe(const int pid, char *buf, size_t len) {

  struct {
    int pid;
    int thid[2];
    char accesspid;
    char accessthid;
    char asid[2];
    char loginname[8];
    char flag;
    char len;
  } Input_data;

  union {
    struct {
      char gthb[4];
      int pid;
      int thid[2];
      char accesspid;
      char accessthid[3];
      int lenused;
      int offsetProcess;
      int offsetConTTY;
      int offsetPath;
      int offsetCommand;
      int offsetFileData;
      int offsetThread;
    } Output_data;
    char buf[2048];
  } Output_buf;

  struct Output_path_type {
    char gthe[4];
    short int len;
    char path[1024];
  } ;

  int Input_length = PGTH_LEN;
  int Output_length = sizeof(Output_buf);
  void *Input_address = &Input_data;
  void *Output_address = &Output_buf;
  struct Output_path_type *Output_path;
  int rv; 
  int rc; 
  int rsn;

  memset(&Input_data, 0, sizeof Input_data);
  Input_data.flag |= PGTHAPATH;
  Input_data.pid = pid;
  Input_data.accesspid = PGTH_CURRENT;

#ifdef _LP64
  BPX4GTH(&Input_length,
          &Input_address,
          &Output_length,
          &Output_address,
          &rv,
          &rc,
          &rsn);
#else
  BPX1GTH(&Input_length,
          &Input_address,
          &Output_length,
          &Output_address,
          &rv,
          &rc,
          &rsn);
#endif

  if (rv == -1) {
    errno = rc;
    return -1;
  }


  assert( ((Output_buf.Output_data.offsetPath >>24) & 0xFF) == 'A');
  Output_path = (char*)(&Output_buf) + 
      (Output_buf.Output_data.offsetPath & 0x00FFFFFF);
  
  if (Output_path->len >= len) {
    errno = ENOBUFS;
    return -1;
  }

  strncpy(buf, Output_path->path, len);

  return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
  unsigned nano;
  unsigned seconds;
  unsigned events=32;
  unsigned secrem;
  unsigned nanorem;
  int rv, rc, rsn;

  nano = req->tv_nsec;
  seconds = req->tv_sec;
  events = CW_CONDVAR;
  
#if defined(_LP64)
  BPX4CTW(&seconds, &nano, &events, &secrem, &nanorem, &rv, &rc, &rsn);
#else
  BPX1CTW(&seconds, &nano, &events, &secrem, &nanorem, &rv, &rc, &rsn);
#endif

  if(rem != NULL) {
    rem->tv_nsec = nanorem;
    rem->tv_sec = secrem;
  }

  if(rv == -1 && errno == EAGAIN)
    return 0;
  
  errno = ENOTSUP;
  return -1;
}
