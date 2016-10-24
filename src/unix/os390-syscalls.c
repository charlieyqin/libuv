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
static QUEUE global_epoll_queue;
static uv_mutex_t global_epoll_lock;
static uv_once_t once = UV_ONCE_INIT;

int scandir(const char* maindir, struct dirent*** namelist,
            int (*filter)(const struct dirent*),
            int (*compar)(const struct dirent**,
            const struct dirent **)) {
  struct dirent** nl;
  struct dirent* dirent;
  unsigned count;
  size_t allocated;
  DIR* mdir;

  nl = NULL;
  count = 0;
  allocated = 0;
  mdir = opendir(maindir);
  if (!mdir)
    return -1;

  while (1) {
    dirent = readdir(mdir);
    if (!dirent)
      break;
    if (!filter || filter(dirent)) {
      struct dirent* copy;
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

      nl = uv__realloc(nl, sizeof(*copy) * (count + 1));
      nl[count++] = copy;
    }
  }

  qsort(nl, count, sizeof(struct dirent *),
       (int (*)(const void *, const void *)) compar);

  closedir(mdir);

  *namelist = nl;
  return count;
}


static int isfdequal(const struct pollfd* first,
                     const struct pollfd* second) {
  return first->fd == second->fd ? 0 : 1;
}


static struct pollfd* accessPfd(uv__os390_epoll* ep, int fd, int events) {
  struct pollfd pfd;
  struct pollfd* found;

  uv_mutex_lock(&global_epoll_lock);
  pfd.fd = fd;
  pfd.events = events;
  if (events == 0)
    found = lfind(&pfd, &ep->items[0], &ep->size,
                  sizeof(pfd), isfdequal);
  else
    found = lsearch(&pfd, &ep->items[0], &ep->size,
                  sizeof(pfd), isfdequal);

  uv_mutex_unlock(&global_epoll_lock);
  return found;
}


static void epoll_init() {
  QUEUE_INIT(&global_epoll_queue);
  if (uv_mutex_init(&global_epoll_lock))
    abort();
}


uv__os390_epoll* epoll_create1(int flags) {
  uv__os390_epoll* lst;

  uv_once(&once, epoll_init);
  uv_mutex_lock(&global_epoll_lock);
  lst = uv__malloc(sizeof(*lst));
  if (lst == -1)
    return NULL;
  QUEUE_INSERT_TAIL(&global_epoll_queue, &lst->member);
  uv_mutex_unlock(&global_epoll_lock);

  /* initialize list */
  memset(lst, 0, sizeof(*lst));
  QUEUE_INIT(&lst->member);
  lst->size = 0;
  return lst; 
}


int epoll_ctl(uv__os390_epoll* lst, int op, int fd, 
              struct epoll_event *event) {
  if(op == EPOLL_CTL_DEL) {
    struct pollfd* found;

    found = accessPfd(lst, fd, 0); 
    if (found == NULL)
      return ENOENT;

    memcpy(found,
           found+1,
           (&lst->items[lst->size--] - found - 1) * sizeof(*found));

  } else if(op == EPOLL_CTL_ADD) {
    struct pollfd* found;
    size_t before;
    size_t after;

    if (lst->size == MAX_ITEMS_PER_EPOLL - 1)
      return ENOMEM;

    before = lst->size; 
    found = accessPfd(lst, fd, event->events);
    after = lst->size; 

    if (found != NULL && before == after) {
      errno = EEXIST;
      return -1;
    }
  } else if(op == EPOLL_CTL_MOD) {
    struct pollfd* found;

    found = accessPfd(lst, fd, 0);
    if (found != NULL)
      found->events = event->events;
    
    if (found == NULL) {
      errno = ENOENT;
      return -1;
    }
  } else
    abort();

  return 0;
}


int epoll_wait(uv__os390_epoll* lst, struct epoll_event* events,
               int maxevents, int timeout) {
  size_t size;
  struct pollfd* pfds;
  int pollret;
  int reventcount;

  uv_mutex_lock(&global_epoll_lock);
  uv_mutex_unlock(&global_epoll_lock);
  size = lst->size;
  pfds = lst->items;
  pollret = poll(pfds, size, timeout);
  if(pollret == -1)
    return pollret;

  reventcount = 0;
  for (int i = 0; i < lst->size && i < maxevents; ++i) {
    struct epoll_event ev;

    ev.events = 0;
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


int epoll_file_close(int fd) {
  QUEUE* q;

  uv_mutex_lock(&global_epoll_lock);
  QUEUE_FOREACH(q, &global_epoll_queue) {
    uv__os390_epoll* lst;
    struct pollfd* found;

    lst = QUEUE_DATA(q, uv__os390_epoll, member);
    found = accessPfd(lst, fd, 0);
    if (found != NULL)
      memcpy(found,
             found+1,
             (&lst->items[lst->size--] - found - 1) * sizeof(*found));
  }

  uv_mutex_unlock(&global_epoll_lock);
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
