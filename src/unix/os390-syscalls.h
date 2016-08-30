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


#ifndef UV_OS390_SYSCALL_H_
#define UV_OS390_SYSCALL_H_

#include "uv.h"

#include <dirent.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>

#define EPOLL_CTL_ADD             1
#define EPOLL_CTL_DEL             2
#define EPOLL_CTL_MOD             3
#define EPOLL_CTL_ADD_MSGQ        4
#define MAX_EPOLL_INSTANCES       256
#define MAX_ITEMS_PER_EPOLL       1024

#define UV__O_CLOEXEC             0x80000
#define UV__EPOLL_CLOEXEC         UV__O_CLOEXEC
#define UV__EPOLL_CTL_ADD         EPOLL_CTL_ADD
#define UV__EPOLL_CTL_DEL         EPOLL_CTL_DEL
#define UV__EPOLL_CTL_MOD         EPOLL_CTL_MOD
#define UV__EPOLL_CTL_ADD_MSGQ    EPOLL_CTL_ADD_MSGQ


typedef union epoll_data {
  int fd;
} epoll_data_t;

struct epoll_event {
  uint32_t events;      /* Epoll events */
  epoll_data_t data;        /* User data variable */
};

struct _epoll_list{
  struct pollfd items[MAX_ITEMS_PER_EPOLL];
  struct pollfd *aio;
  int size;
  pthread_mutex_t lock;
};

#define uv__async_connect uv__zos_aio_connect
#define uv__async_write(req, stream, buf, len) \
        uv__zos_aio_write(req, stream, buf, len, 0)
#define uv__async_writev(req, stream, buf, len) \
        uv__zos_aio_write(req, stream, buf, len, 1)
#define uv__async_read(stream, buf, len) \
        uv__zos_aio_read(stream, &buf, &len)
#define uv__async_accept(stream) \
        uv__zos_aio_accept(stream)

/* epoll api */
int epoll_create1(int flags);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, int sigmask);
int epoll_file_close(int fd);

/* aio interface */
int uv__zos_aio_connect(uv_connect_t *req, uv_stream_t *str,
                         const struct sockaddr* addr,
                         unsigned int addrlen);

int uv__zos_aio_write(uv_write_t *req, uv_stream_t *str,
                         char *buf, int len, int vec);

int uv__zos_aio_read(uv_stream_t *str,
                     char **buf, unsigned long *len);

int uv__zos_aio_accept(uv_stream_t *stream);

/* utility functions */
int nanosleep(const struct timespec *req, struct timespec *rem);
int alphasort(const void *a, const void *b);
int scandir(const char *maindir, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **,
            const struct dirent **));


#endif /* UV_OS390_SYSCALL_H_ */
