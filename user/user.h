struct stat;
struct rtcdate;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int chpri( int, int );
int sh_var_read(void);
int sh_var_write(int);
int sem_create (int n_sem);
int sem_p(int sem_id);
int sem_v(int sem_id);	
int sem_free (int sem_id);
void* shmgetat(uint key, uint num);
int shmrefcount(uint key);
int mqget(uint);
int msgsnd(uint, int, int, char*);
int msgrcv(uint, int, int, uint64);
int clone(void (*fcn)(void *), void *stack, void *arg); 
int join();
uint64 myalloc(int);
int myfree(uint64);
int getcpuid(void);
// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void fprintf(int, const char*, ...);
void printf(const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
