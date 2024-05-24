#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
	printf("hello world,my cpupid: %d\n",getcpuid());
	return 0;
}
