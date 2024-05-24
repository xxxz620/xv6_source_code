#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char const *argv[])
{
    int pid;
    printf("Prio-schedule test\n");
    pid = getpid();
    chpri(pid,19);

    int i = 0;

    pid = fork();
    if (pid == 0)
    {
        chpri(getpid(), 5);
        i = 1;
        while(i <= 10000)
        {
            if (i % 10000 == 0)
                printf("p2 is running\n");
            i++;
        }

        printf("p2 sleeping\n");
        sleep(100);
        i = 1;
        while(i <= 1000000)
        {
            if(i % 1000000 == 0)
                printf("p2 is running again\n");
            i++;
        }
        printf("p2 finished\n");
    }
    else
    {
        i = 1;
        while(i > 0)
        {
            if(i % 1000000 == 0)
                printf("p1 is running\n");
            i++;
            if(i > 1000000)
                break;
        }
    }
    exit(0);
}
