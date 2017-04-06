#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#include "skynet_daemon.h"

static int
check_pid(const char *pidfile) {
	int pid = 0;
	FILE *f = fopen(pidfile,"r");
	if (f == NULL)
		return 0;
	int n = fscanf(f,"%d", &pid);
	fclose(f);

	if (n !=1 || pid == 0 || pid == getpid()) {
		return 0;
	}

	if (kill(pid, 0) && errno == ESRCH) //杀进程
		return 0;

	return pid;
}

static int
write_pid(const char *pidfile) {
	FILE *f;
	int pid = 0;
	int fd = open(pidfile, O_RDWR|O_CREAT, 0644);
	if (fd == -1) {
		fprintf(stderr, "Can't create pidfile [%s].\n", pidfile); //打印到错误的标准输出
		return 0;
	}
	f = fdopen(fd, "r+"); //取文件句柄 使用它一个标准i/o流与之结合
	if (f == NULL) {
		fprintf(stderr, "Can't open pidfile [%s].\n", pidfile);
		return 0;
	}

	if (flock(fd, LOCK_EX|LOCK_NB) == -1) { //文件所 排他所一个文件只被一个进程读取，LOCK_NB尝试加锁已经锁住会返回错误（非阻塞）
		int n = fscanf(f, "%d", &pid); //格式化i/o流 得到pid
		fclose(f);
		if (n != 1) {
			fprintf(stderr, "Can't lock and read pidfile.\n");
		} else {
			fprintf(stderr, "Can't lock pidfile, lock is held by pid %d.\n", pid);
		}
		return 0;
	}

	pid = getpid(); //获得进程pid
	if (!fprintf(f,"%d\n", pid)) { //写入文件
		fprintf(stderr, "Can't write pid.\n");
		close(fd);
		return 0;
	}
	fflush(f);

	return pid;
}

static int
redirect_fds() {
	int nfd = open("/dev/null", O_RDWR);
	if (nfd == -1) {
		perror("Unable to open /dev/null: "); //int dup2(int oldfd,int newfd); 复制文件描述符
		return -1;
	}
	if (dup2(nfd, 0) < 0) {
		perror("Unable to dup2 stdin(0): ");
		return -1;
	}
	if (dup2(nfd, 1) < 0) {
		perror("Unable to dup2 stdout(1): ");
		return -1;
	}
	if (dup2(nfd, 2) < 0) {
		perror("Unable to dup2 stderr(2): ");
		return -1;
	}

	close(nfd);

	return 0;
}

int
daemon_init(const char *pidfile) {
	int pid = check_pid(pidfile); //得到pid

	if (pid) {
		fprintf(stderr, "Skynet is already running, pid = %d.\n", pid);
		return 1;
	}

#ifdef __APPLE__
	fprintf(stderr, "'daemon' is deprecated: first deprecated in OS X 10.5 , use launchd instead.\n");
#else
	if (daemon(1,1)) {
		fprintf(stderr, "Can't daemonize.\n");
		return 1;

		//int daemon(int nochdir, int noclose); 
		//参数： nochdir:当此参数为0时，会更改创建出的danmon的执行目录为根目录，否则（非0）时保持当前执行目录不变a
		//noclose：当次函数为0时，会将标准输入(0)，标准输出(1)，标准错误(2)重定向到/dev/null，否则保持原有标准输入(0)，标准输出(1)，标准错误(2)不变
	}
#endif

	pid = write_pid(pidfile);
	if (pid == 0) {
		return 1;
	}

	if (redirect_fds()) {
		return 1;
	}

	return 0;
}

int
daemon_exit(const char *pidfile) {
	return unlink(pidfile);
}
