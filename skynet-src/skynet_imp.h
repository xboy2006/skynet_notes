#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

//skynet 配置结构
struct skynet_config {
	int thread;    //线程数
	int harbor;    //harbor id
	int profile; 
	const char * daemon; //后台模式启动 "./skynet.pid" 
	const char * module_path; //模块 服务路径 .so文件路径
	const char * bootstrap;   //启动的第一个服务及其参数 默认 "snlua bootstrap"
	const char * logger;      //日志文件
	const char * logservice;  //日志服务 默认logger
};

#define THREAD_WORKER 0
#define THREAD_MAIN 1    //主线程pthread_key的值 
#define THREAD_SOCKET 2
#define THREAD_TIMER 3
#define THREAD_MONITOR 4

void skynet_start(struct skynet_config * config);

#endif
