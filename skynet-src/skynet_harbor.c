#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_server.h"
#include "skynet_mq.h"
#include "skynet_handle.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

/*
 服务地址是一个 32bit 整数，同一进程内的地址的高 8bit 相同。这 8bit 区分了一个服务处于那个节点。
 每个节点中有一个特殊的服务叫做 harbor (港口) ，当一个消息的目的地址的高 8 位和本节点不同时，
 消息被投递到 harbor 服务中，它再通过 tcp 连接传输到目的节点的 harbor 服务中。

 不同的 skynet 节点的 harbor 间是如何建立起网络的呢？
 这依赖一个叫做 master 的服务。这个 master 服务可以单独为一个进程，也可以附属在某一个 skynet 节点内部（默认配置）。

 master 会监听一个端口（在 config 里配置为 standalone 项），每个 skynet 节点都会根据 config 中的 master 项去连接 master 。
 master 再安排不同的 harbor 服务间相互建立连接。
 master 又和所有的 harbor 相连

*/

//启动节点服务，以及注册和发消息给远程节点。

// harbor 服务对应的 skynet_context 指针
//harbor 用来与远程主机通信 master 统一来管理
static struct skynet_context * REMOTE = 0;
static unsigned int HARBOR = ~0;

void 
skynet_harbor_send(struct remote_message *rmsg, uint32_t source, int session) {
	int type = rmsg->sz >> MESSAGE_TYPE_SHIFT; // 高  8 bite 用于保存 type
	rmsg->sz &= MESSAGE_TYPE_MASK;
	assert(type != PTYPE_SYSTEM && type != PTYPE_HARBOR && REMOTE);
	skynet_context_send(REMOTE, rmsg, sizeof(*rmsg) , source, type , session);
}

// 判断消息是不是来自远程主机的
int 
skynet_harbor_message_isremote(uint32_t handle) {
	assert(HARBOR != ~0);
	int h = (handle & ~HANDLE_MASK); // 取高8位
	return h != HARBOR && h !=0;
}

void
skynet_harbor_init(int harbor) {
	HARBOR = (unsigned int)harbor << HANDLE_REMOTE_SHIFT; // 高8位就是对应远程主机通信的 harbor
}

void
skynet_harbor_start(void *ctx) {
	// the HARBOR must be reserved to ensure the pointer is valid.
	// It will be released at last by calling skynet_harbor_exit
	skynet_context_reserve(ctx);
	REMOTE = ctx;
}

void
skynet_harbor_exit() {
	struct skynet_context * ctx = REMOTE;
	REMOTE= NULL;
	if (ctx) {
		skynet_context_release(ctx);
	}
}
