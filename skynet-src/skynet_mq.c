#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "spinlock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define DEFAULT_QUEUE_SIZE 64   //默认队列大小
#define MAX_GLOBAL_MQ 0x10000  //最大的全局消息队列的大小 64K

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.

#define MQ_IN_GLOBAL 1   //在全局队列中或者正在分发
#define MQ_OVERLOAD 1024

//消息队列结构
struct message_queue {
	struct spinlock lock;
	uint32_t handle;  //句柄
	int cap;          //队列大小
	int head;         //队列头
	int tail;         //队列尾
	int release;      //释放
	int in_global;    //全局队列
	int overload;
	int overload_threshold;
	struct skynet_message *queue; //存放具体消息的连续内存的指针
	struct message_queue *next;  //下一个队列的指针
};

//全局消息队列链表 其中保存了非空的各个服务的消息队列message_queue
struct global_queue {
	struct message_queue *head;
	struct message_queue *tail;
	struct spinlock lock;
};

static struct global_queue *Q = NULL; //全局队列的指针变量

//消息队列挂在全局消息链表的尾部
void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q;

	SPIN_LOCK(q)
	assert(queue->next == NULL);
	if(q->tail) {
		q->tail->next = queue;
		q->tail = queue;
	} else {
		q->head = q->tail = queue;
	}
	SPIN_UNLOCK(q)
}

//取出全局消息队列链表头部的消息队列
struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;

	SPIN_LOCK(q)
	struct message_queue *mq = q->head;
	if(mq) {
		q->head = mq->next;
		if(q->head == NULL) {
			assert(mq == q->tail);
			q->tail = NULL;
		}
		mq->next = NULL;
	}
	SPIN_UNLOCK(q)

	return mq;
}


//创建消息队列
struct message_queue * 
skynet_mq_create(uint32_t handle) {
	struct message_queue *q = skynet_malloc(sizeof(*q)); //创建消息队列
	q->handle = handle; //记录handle
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	SPIN_INIT(q)
	// When the queue is create (always between service create and service init) ,
	// set in_global flag to avoid push it to global queue .
	// If the service init success, skynet_context_new will call skynet_mq_push to push it to global queue.
	q->in_global = MQ_IN_GLOBAL; //在全局队列中
	q->release = 0;
	q->overload = 0;
	q->overload_threshold = MQ_OVERLOAD;
	q->queue = skynet_malloc(sizeof(struct skynet_message) * q->cap); //分配连续的cap内存用于存放具体消息
	q->next = NULL;

	return q;
}

static void 
_release(struct message_queue *q) {
	assert(q->next == NULL);
	SPIN_DESTROY(q)
	skynet_free(q->queue);
	skynet_free(q);
}

uint32_t 
skynet_mq_handle(struct message_queue *q) {
	return q->handle;
}

//消息队列的长度
int
skynet_mq_length(struct message_queue *q) {
	int head, tail,cap;

	SPIN_LOCK(q)
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	SPIN_UNLOCK(q)
	
	if (head <= tail) {
		return tail - head;
	}
	return tail + cap - head;
}

int
skynet_mq_overload(struct message_queue *q) {
	if (q->overload) {
		int overload = q->overload;
		q->overload = 0;
		return overload;
	} 
	return 0;
}

//弹出消息队列中的头部消息
int
skynet_mq_pop(struct message_queue *q, struct skynet_message *message) {
	int ret = 1;
	SPIN_LOCK(q)

	if (q->head != q->tail) { //如果队列头不等于队列尾
		*message = q->queue[q->head++]; //取出队列头
		ret = 0; //弹出成功返回0
		int head = q->head;
		int tail = q->tail;
		int cap = q->cap;

		if (head >= cap) { //如果队列头 >= 最大队列数
			q->head = head = 0;
		}
		int length = tail - head;
		if (length < 0) {
			length += cap;
		}
		while (length > q->overload_threshold) {
			q->overload = length;
			q->overload_threshold *= 2;
		}
	} else {
		// reset overload_threshold when queue is empty
		q->overload_threshold = MQ_OVERLOAD;
	}

	if (ret) { 
		q->in_global = 0; //设置在全局状态为0
	}
	
	SPIN_UNLOCK(q)

	return ret;
}

//扩展消息队列message_queue 中的存放消息的内存空间
static void
expand_queue(struct message_queue *q) {
	struct skynet_message *new_queue = skynet_malloc(sizeof(struct skynet_message) * q->cap * 2);
	int i;
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2;
	
	skynet_free(q->queue);
	q->queue = new_queue;
}

//消息压入消息队列 如果标识为在全局中 则将消息队列挂在到全局消息队列的尾部
void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	SPIN_LOCK(q)

	q->queue[q->tail] = *message; //将消息压入消息队列的尾
	if (++ q->tail >= q->cap) {
		q->tail = 0;
	}

	if (q->head == q->tail) { //如果队列头等于队列尾 扩容
		expand_queue(q);
	}

	if (q->in_global == 0) { //如果在全局标志等于0  设置标志为在全局 压入全局消息队列
		q->in_global = MQ_IN_GLOBAL;
		skynet_globalmq_push(q);
	}
	
	SPIN_UNLOCK(q)
}

//初始化全局消息队列 分配和初始struct global_queue内存结构
void 
skynet_mq_init() {
	struct global_queue *q = skynet_malloc(sizeof(*q));
	memset(q,0,sizeof(*q));
	SPIN_INIT(q);
	Q=q;
}

//标记消息队列release = 1 并且将消息队列放入全局消息队列链表
void 
skynet_mq_mark_release(struct message_queue *q) {
	SPIN_LOCK(q)
	assert(q->release == 0);
	q->release = 1;
	if (q->in_global != MQ_IN_GLOBAL) {
		skynet_globalmq_push(q);
	}
	SPIN_UNLOCK(q)
}

static void
_drop_queue(struct message_queue *q, message_drop drop_func, void *ud) {
	struct skynet_message msg;
	while(!skynet_mq_pop(q, &msg)) { //消息弹出消息队列 删除消息队列中的消息内存块数据
		drop_func(&msg, ud);
	}
	_release(q);
}

//释放消息队列  消息弹出消息队列 删除消息队列中的消息内存块数据
void 
skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud) {
	SPIN_LOCK(q)
	
	if (q->release) {
		SPIN_UNLOCK(q)
		_drop_queue(q, drop_func, ud);
	} else {
		skynet_globalmq_push(q);
		SPIN_UNLOCK(q)
	}
}
