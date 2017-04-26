#include "skynet.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

//logger.so
// skynet的日志log服务
struct logger {
	FILE * handle; //文件句柄
	char * filename; //文件名
	int close;
};

struct logger *
logger_create(void) {
	struct logger * inst = skynet_malloc(sizeof(*inst));
	inst->handle = NULL;
	inst->close = 0;
	inst->filename = NULL;

	return inst;
}

void
logger_release(struct logger * inst) {
	if (inst->close) {
		fclose(inst->handle);
	}
	skynet_free(inst->filename);
	skynet_free(inst);
}

static int
logger_cb(struct skynet_context * context, void *ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct logger * inst = ud;
	switch (type) {
	case PTYPE_SYSTEM:
		if (inst->filename) {
			inst->handle = freopen(inst->filename, "a", inst->handle); //以追加的方式打开文件
		}
		break;
	case PTYPE_TEXT: 
		fprintf(inst->handle, "[:%08x] ",source); //想句柄输出日志
		fwrite(msg, sz , 1, inst->handle);
		fprintf(inst->handle, "\n");
		fflush(inst->handle);
		break;
	}

	return 0;
}

int
logger_init(struct logger * inst, struct skynet_context *ctx, const char * parm) {
	if (parm) {
		inst->handle = fopen(parm,"w"); //打开日志文件保存文件句柄
		if (inst->handle == NULL) {
			return 1;
		}
		inst->filename = skynet_malloc(strlen(parm)+1); //保存文件名
		strcpy(inst->filename, parm);
		inst->close = 1;
	} else {
		inst->handle = stdout; //没有配置日志文件则使用默认输出作文日志文件句柄
	}
	if (inst->handle) {
		skynet_callback(ctx, inst, logger_cb); //设置日志文件服务skynet_ctx的cb字段 即消息处理函数
		skynet_command(ctx, "REG", ".logger"); //给服务器注册一个名字 保存在H结构。.logger对用skynet_context.handle放在handle_storage(H)中
		return 0;
	}
	return 1;
}
