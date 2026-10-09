#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>
#include <unistd.h>
int main() {
    int fd = inotify_init();
    int wd = inotify_add_watch(fd, "./canary", IN_MODIFY | IN_CREATE | IN_DELETE);
    printf("wd = %d\n", wd);
    char buf[4096];
    int len = read(fd, buf, sizeof(buf));
    printf("read %d bytes\n", len);
    return 0;
}
