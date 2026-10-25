#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/event.h>
#include <unistd.h>
int main(void) {
  int q = kqueue();
  if (q < 0)
    return 1;
  int (*callback)(int) = dlsym(RTLD_DEFAULT, "kqueue_close");
  if (!callback)
    return 2;
  int handled = callback(q);
  printf("libSystem close callback recognized public queue=%d\n", handled);
  if (!handled) {
    close(q);
    return 3;
  }
  errno = 0;
  if (fcntl(q, F_GETFD) != -1 || errno != EBADF)
    return 4;
  puts("Public queue and close callback share a registry");
  return 0;
}
