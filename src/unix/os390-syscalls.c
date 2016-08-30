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

#define CW_CONDVAR 32

#pragma linkage(BPX4CTW, OS)
#pragma linkage(BPX1CTW, OS)
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
