#ifndef SKYNET_RWLOCK_H
#define SKYNET_RWLOCK_H

#ifndef USE_PTHREAD_LOCK

struct rwlock {
	int write;
	int read;
};

static inline void
rwlock_init(struct rwlock *lock) {
	lock->write = 0;
	lock->read = 0;
}

static inline void
rwlock_rlock(struct rwlock *lock) {
	for (;;) {
		while(lock->write) {
			__sync_synchronize(); //确保读取lock->write都是最新的
		}
		__sync_add_and_fetch(&lock->read,1); //先fetch，然后自加，返回的是自加以前的值。 加减的原子操作
		if (lock->write) {
			__sync_sub_and_fetch(&lock->read,1);
		} else {
			break;
		}
	}
}

static inline void
rwlock_wlock(struct rwlock *lock) {
	while (__sync_lock_test_and_set(&lock->write,1)) {} //将*ptr设为value并返回*ptr操作之前的值。lock->write 设置为1
	while(lock->read) {
		__sync_synchronize();
	}
}

static inline void
rwlock_wunlock(struct rwlock *lock) {
	__sync_lock_release(&lock->write); //将*ptr置0
}

static inline void
rwlock_runlock(struct rwlock *lock) {
	__sync_sub_and_fetch(&lock->read,1);
}

#else

#include <pthread.h>

// only for some platform doesn't have __sync_*
// todo: check the result of pthread api //对于读数据比修改数据频繁的应用，用读写锁代替互斥锁可以提高效率

struct rwlock {
	pthread_rwlock_t lock; //读写锁
};

static inline void
rwlock_init(struct rwlock *lock) {
	pthread_rwlock_init(&lock->lock, NULL); //读写锁初始化
}

static inline void
rwlock_rlock(struct rwlock *lock) {
	 pthread_rwlock_rdlock(&lock->lock); //等待读锁
}

static inline void
rwlock_wlock(struct rwlock *lock) {
	 pthread_rwlock_wrlock(&lock->lock); //等待写锁
}

static inline void
rwlock_wunlock(struct rwlock *lock) {
	pthread_rwlock_unlock(&lock->lock); //解锁
}

static inline void
rwlock_runlock(struct rwlock *lock) {
	pthread_rwlock_unlock(&lock->lock); //解锁
}

#endif

#endif
