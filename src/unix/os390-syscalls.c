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

static int number_of_epolls;
struct _epoll_list* _global_epoll_list[MAX_EPOLL_INSTANCES];

int scandir(const char *maindir, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **,
            const struct dirent **)) {
  struct dirent **nl = NULL;
  struct dirent *dirent;
  unsigned count = 0;
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
      copy = uv__malloc(sizeof(*copy));
      if (!copy) {
        while (count) {
          dirent = nl[--count];
          uv__free(dirent);
        }
        uv__free(nl);
        closedir(mdir);
        errno = ENOMEM;
        return -1;
      }
      memcpy(copy, dirent, sizeof(*copy));

      nl = uv__realloc(nl, count+1);
      nl[count++] = copy;
    }
  }

  qsort(nl, count, sizeof(struct dirent *),
       (int (*)(const void *, const void *))compar);

  closedir(mdir);

  *namelist = nl;
  return count;
}

static int isfdequal(const void* first, const void* second) {
  const struct pollfd* a = first;
  const struct pollfd* b = second;
  return a->fd == b->fd ? 0 : 1;
}

static struct pollfd* findpfd(int epfd, int fd, int events) {
  struct _epoll_list *lst;
  struct pollfd pfd;
  struct pollfd* found;

  pfd.fd = fd;
  pfd.events = events;
  lst = _global_epoll_list[epfd];
  if (events == 0)
    return lfind(&pfd, &lst->items[0], &lst->size,
                  sizeof(pfd), isfdequal);
  else
    return lsearch(&pfd, &lst->items[0], &lst->size,
                  sizeof(pfd), isfdequal);
}

int epoll_create1(int flags) {
  struct _epoll_list* list;
  int index;

  list = uv__malloc(sizeof(*list));
  memset(list, 0, sizeof(*list));
  index = number_of_epolls++;
  _global_epoll_list[index] = list;

  if (uv_mutex_init(&list->lock)) {
    errno = ENOLCK;
    return -1;
  }

  list->size = 0;
  return index; 
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
  struct _epoll_list *lst;

  lst = _global_epoll_list[epfd];
  uv_mutex_lock(&lst->lock);

  if(op == EPOLL_CTL_DEL) {
    struct pollfd* found;

    found = findpfd(epfd, fd, 0); 
    if (found != NULL)
      memcpy(found,
             found+1,
             (&lst->items[lst->size--] - found - 1) * sizeof(*found));
    uv_mutex_unlock(&lst->lock);

    if (found == NULL)
      return ENOENT;

  }
  else if(op == EPOLL_CTL_ADD) {
    struct pollfd* found;
    size_t before;
    size_t after;

    if (lst->size == MAX_ITEMS_PER_EPOLL - 1) {
      uv_mutex_unlock(&lst->lock);
      return ENOMEM;
    }

    before = lst->size; 
    found = findpfd(epfd, fd, event->events);
    after = lst->size; 
    uv_mutex_unlock(&lst->lock);

    if (found != NULL && before == after) {
      errno = EEXIST;
      return -1;
    }

  }
  else if(op == EPOLL_CTL_MOD) {
    struct pollfd* found;

    found = findpfd(epfd, fd, 0);
    if (found != NULL)
      found->events = event->events;
    
    uv_mutex_unlock(&lst->lock);
    if (found == NULL) {
      errno = ENOENT;
      return -1;
    }
  }
  else {
    abort();
  }
  return 0;
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
  struct _epoll_list *lst;
  size_t size;
  struct pollfd* pfds;
  int pollret;

  lst = _global_epoll_list[epfd];
  uv_mutex_lock(&lst->lock);
  size = lst->size;
  pfds = lst->items;
  pollret = poll(pfds, size, timeout);
  if(pollret == -1) {
    uv_mutex_unlock(&lst->lock);
    return pollret;
  }

  int reventcount=0;
  size_t realsize = lst->size;
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

  uv_mutex_unlock(&lst->lock);
  return reventcount;
}

int epoll_file_close(int fd) {
  for( int i = 0; i < number_of_epolls; ++i )
  {
    struct _epoll_list *lst;
    struct pollfd* found;

    lst = _global_epoll_list[i];
    uv_mutex_lock(&lst->lock);
    found = findpfd(i, fd, 0);
    if (found != NULL)
      memcpy(found,
             found+1,
             (&lst->items[lst->size--] - found - 1) * sizeof(*found));
    uv_mutex_unlock(&lst->lock);
  }

  return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
  unsigned nano;
  unsigned seconds;
  unsigned events;
  unsigned secrem;
  unsigned nanorem;
  int rv;
  int rc;
  int rsn;

  nano = (int)req->tv_nsec;
  seconds = req->tv_sec;
  events = CW_CONDVAR;
  
#if defined(_LP64)
  BPX4CTW(&seconds, &nano, &events, &secrem, &nanorem, &rv, &rc, &rsn);
#else
  BPX1CTW(&seconds, &nano, &events, &secrem, &nanorem, &rv, &rc, &rsn);
#endif

  assert(rv == -1 && errno == EAGAIN);

  if(rem != NULL) {
    rem->tv_nsec = nanorem;
    rem->tv_sec = secrem;
  }

  return 0;
}
