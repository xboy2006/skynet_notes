#ifndef skynet_socket_h
#define skynet_socket_h

struct skynet_context;

#define SKYNET_SOCKET_TYPE_DATA 1
#define SKYNET_SOCKET_TYPE_CONNECT 2
#define SKYNET_SOCKET_TYPE_CLOSE 3
#define SKYNET_SOCKET_TYPE_ACCEPT 4
#define SKYNET_SOCKET_TYPE_ERROR 5
#define SKYNET_SOCKET_TYPE_UDP 6
#define SKYNET_SOCKET_TYPE_WARNING 7

struct skynet_socket_message {
	int type; //消息类型
	int id;   //id
	int ud;
	char * buffer; //数据
};

void skynet_socket_init(); //初始化socket
void skynet_socket_exit(); //退出
void skynet_socket_free(); //释放
int skynet_socket_poll();  //事件循环

int skynet_socket_send(struct skynet_context *ctx, int id, void *buffer, int sz);// 发送数据
void skynet_socket_send_lowpriority(struct skynet_context *ctx, int id, void *buffer, int sz);// 低优先级发送数据
int skynet_socket_listen(struct skynet_context *ctx, const char *host, int port, int backlog);// 监听 Socket
int skynet_socket_connect(struct skynet_context *ctx, const char *host, int port);// Socket 连接
int skynet_socket_bind(struct skynet_context *ctx, int fd);// 绑定事件
void skynet_socket_close(struct skynet_context *ctx, int id);// 关闭 Socket
void skynet_socket_shutdown(struct skynet_context *ctx, int id);
void skynet_socket_start(struct skynet_context *ctx, int id);// 启动 Socket 加入事件循环
void skynet_socket_nodelay(struct skynet_context *ctx, int id);

int skynet_socket_udp(struct skynet_context *ctx, const char * addr, int port);
int skynet_socket_udp_connect(struct skynet_context *ctx, int id, const char * addr, int port);
int skynet_socket_udp_send(struct skynet_context *ctx, int id, const char * address, const void *buffer, int sz);
const char * skynet_socket_udp_address(struct skynet_socket_message *, int *addrsz);

#endif
