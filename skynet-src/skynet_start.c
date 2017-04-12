#include "skynet.h"
#include "skynet_server.h"
#include "skynet_imp.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_module.h"
#include "skynet_timer.h"
#include "skynet_monitor.h"
#include "skynet_socket.h"
#include "skynet_daemon.h"
#include "skynet_harbor.h"

#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

//监控结构
struct monitor { 
	int count; 					//工作线程数量
	struct skynet_monitor ** m;	//monitor 工作线程监控列表
	pthread_cond_t cond;        //条件变量
	pthread_mutex_t mutex;      //互斥锁
	int sleep;                  //睡眠中工作线程数量
	int quit;
};

//工作线程参数
struct worker_parm {
	struct monitor *m;
	int id;
	int weight;
};

static int SIG = 0;

static void
handle_hup(int signal) {
	if (signal == SIGHUP) {
		SIG = 1;
	}
}

#define CHECK_ABORT if (skynet_context_total()==0) break;

static void
create_thread(pthread_t *thread, void *(*start_routine) (void *), void *arg) {
	if (pthread_create(thread,NULL, start_routine, arg)) {
		fprintf(stderr, "Create thread failed");
		exit(1);
	}
}

//当睡眠线程的数量>一定数量才唤醒一个线程
static void
wakeup(struct monitor *m, int busy) {
	if (m->sleep >= m->count - busy) {
		// signal sleep worker, "spurious wakeup" is harmless
		pthread_cond_signal(&m->cond);
	}
}

//socket线程
static void *
thread_socket(void *p) {
	struct monitor * m = p; //接入monitor结构
	skynet_initthread(THREAD_SOCKET); //设置线程局部存储 G_NODE.handle_key 为 THREAD_SOCKET
	for (;;) {
		int r = skynet_socket_poll(); //检测网络事件（epoll管理的网络事件）并且将事件放入消息队列 skynet_socket_poll--->skynet_context_push
		if (r==0) //SOCKET_EXIT
			break;
		if (r<0) {
			CHECK_ABORT
			continue;
		}
		wakeup(m,0); //全部线程睡眠情况下才唤醒一个工作线程
	}
	return NULL;
}

//释放监视
static void
free_monitor(struct monitor *m) {
	int i;
	int n = m->count;
	for (i=0;i<n;i++) {
		skynet_monitor_delete(m->m[i]); //删除skynet_monitor结构
	}
	pthread_mutex_destroy(&m->mutex); //删除互斥锁
	pthread_cond_destroy(&m->cond); //删除条件变量
	skynet_free(m->m);  //释放监视中的skynet_monitor数组指针
	skynet_free(m); //释放监视结构
}

//监视线程 用于监控是否有消息没有即时处理
static void *
thread_monitor(void *p) {
	struct monitor * m = p;
	int i;
	int n = m->count;
	skynet_initthread(THREAD_MONITOR);
	for (;;) {
		CHECK_ABORT
		for (i=0;i<n;i++) { //遍历监视列表
			skynet_monitor_check(m->m[i]);
		}
		for (i=0;i<5;i++) {//睡眠5秒
			CHECK_ABORT
			sleep(1); 
		}
	}

	return NULL;
}

static void
signal_hup() {
	// make log file reopen

	struct skynet_message smsg;
	smsg.source = 0;
	smsg.session = 0;
	smsg.data = NULL;
	smsg.sz = (size_t)PTYPE_SYSTEM << MESSAGE_TYPE_SHIFT;
	uint32_t logger = skynet_handle_findname("logger");
	if (logger) {
		skynet_context_push(logger, &smsg);
	}
}

//定时器线程
static void *
thread_timer(void *p) {
	struct monitor * m = p;
	skynet_initthread(THREAD_TIMER);
	for (;;) {
		skynet_updatetime();//更新 定时器 的时间
		CHECK_ABORT
		wakeup(m,m->count-1); //只要有一个线程睡眠就唤醒 让工作线程动起来
		usleep(2500);//睡眠2500微妙（1秒=1000000微秒）0.025秒
		if (SIG) {
			signal_hup();
			SIG = 0;
		}
	}
	// wakeup socket thread
	skynet_socket_exit();
	// wakeup all worker thread
	pthread_mutex_lock(&m->mutex);
	m->quit = 1; //设置退出标志
	pthread_cond_broadcast(&m->cond); //唤醒所有等待条件变量的线程
	pthread_mutex_unlock(&m->mutex);
	return NULL;
}

//工作线程
static void *
thread_worker(void *p) {
	struct worker_parm *wp = p;
	int id = wp->id;
	int weight = wp->weight;
	struct monitor *m = wp->m;
	struct skynet_monitor *sm = m->m[id]; //通过线程id拿到监视器结构（skynet_monitor）
	skynet_initthread(THREAD_WORKER);
	struct message_queue * q = NULL;
	while (!m->quit) {
		q = skynet_context_message_dispatch(sm, q, weight); //消息调度执行（取出消息 执行服务中的回调函数）
		if (q == NULL) {
			if (pthread_mutex_lock(&m->mutex) == 0) {
				++ m->sleep; //进入睡眠
				// "spurious wakeup" is harmless,
				// because skynet_context_message_dispatch() can be call at any time.
				if (!m->quit)
					pthread_cond_wait(&m->cond, &m->mutex); //没有消息则进入睡眠等待唤醒
				-- m->sleep; //唤醒后减少睡眠线程数量
				if (pthread_mutex_unlock(&m->mutex)) {
					fprintf(stderr, "unlock mutex error");
					exit(1);
				}
			}
		}
	}
	return NULL;
}

static void
start(int thread) { // 线程数+3 3个线程分别用于 _monitor _timer  _socket 监控 定时器 socket IO
	pthread_t pid[thread+3];

	struct monitor *m = skynet_malloc(sizeof(*m)); //初始化monitir结构
	memset(m, 0, sizeof(*m));
	m->count = thread;
	m->sleep = 0;

	m->m = skynet_malloc(thread * sizeof(struct skynet_monitor *));
	int i;
	for (i=0;i<thread;i++) {
		m->m[i] = skynet_monitor_new(); //创建skynet_monitor结构放在监视列表 为每个线程新建一个监视
	}
	if (pthread_mutex_init(&m->mutex, NULL)) { //初始化互斥变量
		fprintf(stderr, "Init mutex error");
		exit(1);
	}
	if (pthread_cond_init(&m->cond, NULL)) {//初始化条件变量
		fprintf(stderr, "Init cond error");
		exit(1);
	}

	create_thread(&pid[0], thread_monitor, m); // 创建 监视 线程
	create_thread(&pid[1], thread_timer, m);   // 创建 定时器 线程
	create_thread(&pid[2], thread_socket, m);  // 创建 网络 线程

	static int weight[] = { 
		-1, -1, -1, -1, 0, 0, 0, 0,
		1, 1, 1, 1, 1, 1, 1, 1, 
		2, 2, 2, 2, 2, 2, 2, 2, 
		3, 3, 3, 3, 3, 3, 3, 3, };
	struct worker_parm wp[thread];
	for (i=0;i<thread;i++) {
		wp[i].m = m;
		wp[i].id = i;
		if (i < sizeof(weight)/sizeof(weight[0])) {
			wp[i].weight= weight[i];
		} else {
			wp[i].weight = 0;
		}
		create_thread(&pid[i+3], thread_worker, &wp[i]);// 创建多个工作线程
	}

	for (i=0;i<thread+3;i++) {
		pthread_join(pid[i], NULL); //阻塞的方式等待线程结束
	}

	free_monitor(m);// 释放 监视
}

static void
bootstrap(struct skynet_context * logger, const char * cmdline) {
	int sz = strlen(cmdline);
	char name[sz+1];
	char args[sz+1];
	sscanf(cmdline, "%s %s", name, args);
	struct skynet_context *ctx = skynet_context_new(name, args);
	if (ctx == NULL) {
		skynet_error(NULL, "Bootstrap error : %s\n", cmdline);
		skynet_context_dispatchall(logger);
		exit(1);
	}
}

//Skynet 启动 传入skynet_config结构体
void 
skynet_start(struct skynet_config * config) {
	// register SIGHUP for log file reopen
	struct sigaction sa;
	sa.sa_handler = &handle_hup;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);

	if (config->daemon) {
		if (daemon_init(config->daemon)) { //后台执行
			exit(1);
		}
	}
	skynet_harbor_init(config->harbor); //初始化节点 编号
	skynet_handle_init(config->harbor); //初始化句柄 编号和skynet_context 初始化一个 handle 就是初始化 handle_storage H
	skynet_mq_init();                   //初始化全局队列 Q
	skynet_module_init(config->module_path);  //初始化模块管理
	skynet_timer_init(); //初始化定时器
	skynet_socket_init(); //初始化SOCKET_SERVER
	skynet_profile_enable(config->profile); //开启性能分析

	struct skynet_context *ctx = skynet_context_new(config->logservice, config->logger); //创建日志 skynet_context 开启日志服务
	if (ctx == NULL) {
		fprintf(stderr, "Can't launch %s service\n", config->logservice);
		exit(1);
	}

	bootstrap(ctx, config->bootstrap); //启动初始服务 

	start(config->thread); //开启各种线程

	// harbor_exit may call socket send, so it should exit before socket_free
	skynet_harbor_exit();//节点管理服务退出
	skynet_socket_free();// 释放网络
	if (config->daemon) {
		daemon_exit(config->daemon);
	}
}
