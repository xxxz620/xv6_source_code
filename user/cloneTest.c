#include "kernel/types.h"
#include "user/user.h"
#define PGSIZE  4096
void sayHello()
{
    printf("hello, I'm child \n");
    return ;
}

int main(void)
{
    uint64 arg[] = {1,2};
    void* stack = malloc(PGSIZE);   // allocate one page for user stack
    clone(sayHello, stack,(void*)arg); // system call for kernel thread
    printf("hello, I'm parenet \n");
    join();
    return 0;
}