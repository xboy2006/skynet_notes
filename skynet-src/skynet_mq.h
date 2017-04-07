#ifndef SKYNET_MESSAGE_QUEUE_H
#define SKYNET_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>

//skynet 内部消息结构
struct skynet_message {
	uint32_t source; //源地址
	int session;     //会话
	void * data;     //数据指针
	size_t sz;       //数据的长度
};

// type is encoding in skynet_message.sz high 8bit
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t)-1) * 8)

struct message_queue; //消息队列

void skynet_globalmq_push(struct message_queue * queue); //压入全局队列
struct message_queue * skynet_globalmq_pop(void);        //弹出全局队列

struct message_queue * skynet_mq_create(uint32_t handle);  //创建消息队列
void skynet_mq_mark_release(struct message_queue *q);      //标记释放消息队列

typedef void (*message_drop)(struct skynet_message *, void *);

void skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud); //释放消息队列
uint32_t skynet_mq_handle(struct message_queue *); //消息队列的句柄

// 0 for success
int skynet_mq_pop(struct message_queue *q, struct skynet_message *message); //消息出队列
void skynet_mq_push(struct message_queue *q, struct skynet_message *message);//消息如队列

// return the length of message queue, for debug
int skynet_mq_length(struct message_queue *q); //消息队列长度
int skynet_mq_overload(struct message_queue *q);

void skynet_mq_init(); //全局消息队列的初始化

#endif
