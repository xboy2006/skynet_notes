#include "skynet.h"
#include "skynet_env.h"
#include "spinlock.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>

//skynet 环境保存lua虚拟机
struct skynet_env {
	struct spinlock lock;   //互斥锁
	lua_State *L;           //lua虚拟机
};

// skynet 环境配置 主要是获取和设置lua的环境变量
static struct skynet_env *E = NULL;

//获取skynet环境变量
const char * 
skynet_getenv(const char *key) {
	SPIN_LOCK(E)

	lua_State *L = E->L;
	
	lua_getglobal(L, key); //获取_G[key]放在栈顶
	const char * result = lua_tostring(L, -1); // 去除栈顶数据 _G[key]
	lua_pop(L, 1); //弹出栈顶一个值

	SPIN_UNLOCK(E)

	return result;
}

//设置skynet环境变量
void 
skynet_setenv(const char *key, const char *value) {
	SPIN_LOCK(E)
	
	lua_State *L = E->L;
	lua_getglobal(L, key);
	assert(lua_isnil(L, -1));
	lua_pop(L,1);
	lua_pushstring(L,value); //value 字符串入栈
	lua_setglobal(L,key); //栈顶元素出栈 设置为G[key] = value的值。
	//Pops a value from the stack and sets it as the new value of global name.

	SPIN_UNLOCK(E)
}

//环境初始化 创建一个lua虚拟机
void
skynet_env_init() {
	E = skynet_malloc(sizeof(*E));
	SPIN_INIT(E)
	E->L = luaL_newstate(); //创建lua虚拟机
}
