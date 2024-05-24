#include "types.h"
#include "param.h"
#include "loongarch.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "memlayout.h"

int findkey(int key);
int newmq(int key);
int reloc(int mqid);

struct msg {  		//消息结构体
    struct msg *next;  					//指向下一个消息
    long type;							// 消息类型
    char *dataaddr;   					//数据地址，实际上等与msg+16
    int  datasize;    					//消息长度
};
 
struct mq {         //消息队列
    int key;        					//对应的key
    int status;    						//0代表未使用，1代表已使用
    struct msg *msgs; 					//指向msg链表
    int maxbytes;     					//一个消息队列最大为4k
    int curbytes;     					//当前已使用字节数
    int refcount;     					//引用数（进程数）
};

struct spinlock mqlock;   				//消息队列 锁
struct mq mqs[MQMAX];  				//默认系统最多8个消息队列
struct proc* wqueue[NPROC];   			//写阻塞队列
int    wstart=0;      			//写阻塞队列指示下标

struct proc* rqueue[NPROC];  			//读阻塞队列
int    rstart=0;     				//读阻塞队列指示下标

void mqinit()
{
    printf("mqinit.\n");
    initlock(&mqlock,"mqlock");
    for(int i =0;i<MQMAX;++i){
        mqs[i].status = 0;
    }
}

void addmqcount(uint mask)
{
    //cprintf("addcount: %x.\n",mask);
    acquire(&mqlock);
    for (int key = 0; key < MQMAX; key++)
    {
        if(mask >> key & 1){
            mqs[key].refcount++;
        }
    }
    release(&mqlock);
}

int mqget(uint key)
{
    struct proc *proc = myproc();
    
    acquire(&mqlock);
    int idx = findkey(key);
    if(idx != -1){                          // 如果key对应的消息队列已经创建
        if(!(proc->mqmask >> idx & 1)){     // 如果当前进程还未使用该消息队列
            proc->mqmask |= 1 << idx;       // 标记本进程使用该消息队列
            mqs[idx].refcount++;            // 消息队列的引用计数+1
        }
        release(&mqlock);
        return idx;
    }                                       // 对应key消息队列未创建则newmq创建
    idx = newmq(key);                       // 创建消息队列
    release(&mqlock);
    return idx;                             // 返回该消息队列在mqs []中的下标
}

int findkey(int key)
{
    int idx =-1;
    for(int i = 0;i<MQMAX;++i){
        if(mqs[i].status != 0 && mqs[i].key == key){
            idx = i;
            break;
        }
    }
    return idx;
}

int newmq(int key)
{
    struct proc *proc = myproc();
    int idx =-1;
    for(int i=0;i<MQMAX;++i){    				//查看mqs数组中是否有空闲项
        if(mqs[i].status == 0){
            idx = i;
            break;
        }
    }
    if(idx == -1){   						//消息队列全部用满，创建失败
        printf("newmq failed: can not get idx.\n");
        return -1;
    }
    mqs[idx].msgs = (struct msg*)kalloc();  		//为消息池分配空间（1个页）
    if(mqs[idx].msgs == 0){					//消息的存储空间不能为NULL
        printf("newmq failed: can not alloc page.\n");
        return -1;
    }
    mqs[idx].key = key;						//为该消息队列设置key值
    mqs[idx].status = 1;						//标示为已启用
    memset(mqs[idx].msgs,0,PGSIZE);     		//清空消息池
    mqs[idx].msgs -> next = 0;         			//接下来都是初始化消息队列
    mqs[idx].msgs -> datasize = 0;
    mqs[idx].maxbytes = PGSIZE;
    mqs[idx].curbytes = 16;
    mqs[idx].refcount = 1;
    proc->mqmask |= 1 << idx;    //修改当前进程的mqmask，表示使用中
    return idx;
}

int reloc(int mqid)
{
    struct msg *pages = mqs[mqid].msgs;      //移动消息目标地址
    struct msg *m  = pages;				//待移动消息
    struct msg *t;
    struct msg *pre = pages;
    while (m != 0)
    {
        t = m->next;						//提前保存下一个待移动消息的地址
        
        memmove(pages, m, m->datasize+32);          //移动消息（包括原地拷贝）
        pages->next = (struct msg *)((char *)pages + pages->datasize + 32);     // 修改下个消息指针
        pages->dataaddr = ((char *)pages + 32);     //修改数据指针
        pre = pages;					//记录当前消息指针
        pages = pages->next;			//下个消息的目标位置（目的）
        m = t;						//下一个待移动消息（源）
    }
    pre->next = 0;					//最后一个消息的next指针置0
    return 0;
}

int msgsnd(uint mqid, int type, int sz, char *msg)
{
    struct proc *proc = myproc();
    if(mqid<0 || MQMAX<=mqid || mqs[mqid].status == 0){
        return -1;
    }

    char *data = msg;
    printf("data:%s\n", data);
 
    if(mqs[mqid].msgs == 0){
        printf("msgsnd failed: msgs == 0.\n");
        return -1;
    }
 
    acquire(&mqlock);
 
    while(1){               //一直循环直到发送成功
        if(mqs[mqid].curbytes + sz + 16 <= mqs[mqid].maxbytes){ //如果剩余空间充裕
            struct msg *m = mqs[mqid].msgs;
            while(m->next != 0){           //找到队尾最后一个空闲消息区
                m = m -> next;
            }                               //退出循环时，m->next==0标示空闲消息区
            m->next = (void *)m + m->datasize + 32; //计算用于存储消息的起始位置
            m = m -> next;              //m为本消息存储空间起点
            m->type = type;             //填写本消息type
            m->next = 0;                    //本消息暂无后续消息
            m->dataaddr = (char *)m + 32;   //数据区
            m->datasize = sz;               //数据长度

            memmove(m->dataaddr, msg, sz);  //拷贝消息数据
             
            mqs[mqid].curbytes += (sz+32);  //可用空间缩减
 
            for(int i=0; i<rstart; i++)     //唤醒所有读阻塞进程
            {
                wakeup(rqueue[i]);
            }
            rstart = 0;                     //读阻塞队列置空

            release(&mqlock);
            return 0;
        } else {                            //空间不足，进程睡眠在wqueue阻塞队列
            printf("msgsnd: can not alloc: pthread: %d sleep.\n",proc->pid);
            wqueue[wstart++] = proc;        //环形队列

            sleep(proc,&mqlock);
        }
        
    }

 	return -1;
}

int
msgrcv(uint mqid, int type, int sz, uint64 addr)
{
    struct proc *proc = myproc();
    if(mqid<0 || MQMAX<=mqid || mqs[mqid].status ==0){
        return -1;
    }
 
 
 
    acquire(&mqlock);
    
    while(1){
        struct msg *m = mqs[mqid].msgs->next;
        struct msg *pre = mqs[mqid].msgs;
        while (m != 0)
        {
            if(m->type == type){        //找到要读取的消息类型
                copyoutstr(proc->pagetable, addr, m->dataaddr, sz);

                pre->next = m->next;    //将已读取的消息从消息队列中删除
                mqs[mqid].curbytes -= (m->datasize + 32);   //释放消息空间
                reloc(mqid);            //重新整理内存

                for(int i=0; i<wstart; i++) //唤醒写阻塞进程
                {
                    wakeup(wqueue[i]);
                }
                wstart = 0;                 //写阻塞队列置空
 
                release(&mqlock);
                return 0;
            }
            pre = m;
            m = m->next;
        }
        printf("msgrcv: can not read: pthread: %d sleep.\n",proc->pid);
        rqueue[rstart++] = proc;
        sleep(proc,&mqlock);
    }
    return -1;
}

void
rmmq(int mqid)
{
    //cprintf("rmmq: %d.\n",mqid);
    kfree((char *)mqs[mqid].msgs);  //回收物理内存
    mqs[mqid].status = 0;
}
 
void
releasemq(uint key)
{
   //cprintf("releasemq: %d.\n",key);
  int idx= findkey(key);
  if (idx!=-1){
      acquire(&mqlock);
          mqs[idx].refcount--;   //引用数目减1
            if(mqs[idx].refcount == 0)  //引用数目为0时候需要回收物理内存
                rmmq(idx);
       release(&mqlock);
     }
}

void
releasemq2(int mask)
{
    //cprintf("releasemq: %x.\n",mask);
    acquire(&mqlock);
    for(int id = 0;id<MQMAX;++id){
        if( mask >> id & 0x1){
            mqs[id].refcount--;   //引用数目减1
            if(mqs[id].refcount == 0){  //引用数目为0时候需要回收物理内存
                rmmq(id);
            }
        }
    }
    release(&mqlock);
}

