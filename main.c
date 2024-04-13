#include <time.h>
#include <stdio.h>

int main() {
    time_t btime = time(NULL);
    char *x = ctime(&btime);
    printf("%s", x);
    return 0;
}
