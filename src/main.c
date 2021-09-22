/*
 * PlayStation(R)Vita Http Server
 * Copyright (C) 2020 Princess of Slepping
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>

#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <psp2/ctrl.h>
#include <psp2/libssl.h>
#include <psp2/shellutil.h>
#include <psp2/sysmodule.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "graphics.h"

#define printf psvDebugScreenPrintf

#define BASE_PATH "host0:www"

char initparam_buf[0x4000];

static int rsock, len;

const char *sceClibStrchr(const char *s, int ch);

typedef struct ScePafInit {
	SceSize global_heap_size;
	int a2;
	int a3;
	int use_gxm;
	int heap_opt_param1;
	int heap_opt_param2;
} ScePafInit; // size is 0x18

typedef struct ScePafHeapInfo {
	void *a1;                    // some paf ptr
	void *a2;                    // membase
	void *a3;                    // membase top
	SceSize size;
	char name[0x20];             // off:0x10
	char data_0x30[4];
	int data_0x34;
	SceKernelLwMutexWork lw_mtx; // off:0x38
	SceUID memblk_id;            // off:0x58
	int data_0x5C;               // ex:1
} ScePafHeapInfo; // size is 0x60

typedef struct ScePafHeapOpt {
	int a1;
	int a2;
	char a3[4];
	int a4;
	int a5;
} ScePafHeapOpt; // size is 0x14

ScePafHeapInfo *scePafHeapInit(ScePafHeapInfo *pInfo, void *membase, SceSize size, const char *name, ScePafHeapOpt *pOpt);
ScePafHeapInfo *scePafHeapFini(ScePafHeapInfo *pInfo);

void *scePafMallocWithInfo(ScePafHeapInfo *pInfo, SceSize len);
void scePafFree(void *ptr);

ScePafHeapInfo heap_info;

void *my_malloc(SceSize len){

	void *res;

	res = scePafMallocWithInfo(&heap_info, len);

	while(res == NULL){
		sceKernelDelayThread(600);
		res = scePafMallocWithInfo(&heap_info, len);
	}

	return res;
}

void my_free(void *ptr){
	scePafFree(ptr);
}

int paf_init(void){

	int load_res;
	ScePafInit init_param;
	SceSysmoduleOpt sysmodule_opt;

	sceClibMemset(&heap_info, 0, sizeof(heap_info));

	init_param.global_heap_size = 0x8000;
	init_param.a2               = 0xFFFFFFFF;
	init_param.a3               = 0xFFFFFFFF;
	init_param.use_gxm          = 0;
	init_param.heap_opt_param1  = 1;
	init_param.heap_opt_param2  = 1;

	load_res = 0xFFFFFFFF;
	sysmodule_opt.flags  = 0x10; // with arg
	sysmodule_opt.result = &load_res;

#define HEAP_SIZE (0x100000 * 32)

	SceUID memid;
	void *heap_base;

	memid = sceKernelAllocMemBlock("SceServerHeap", 0x0C20D060, HEAP_SIZE, NULL);

	sceKernelGetMemBlockBase(memid, &heap_base);

	sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(init_param), &init_param, &sysmodule_opt);

	scePafHeapInit(&heap_info, heap_base, HEAP_SIZE, "ScePafHeap", NULL);

	return 0;
}

typedef struct HttpSendParam {
	int wsock;
	SceUID thid;
	SceUID thid_work;
	SceUID memblk_uid;
	void *arg;
	char remote_ip[16];
} HttpSendParam;

int getUriUnescape(char *dst, SceSize len, const char *path){

	int res;
	SceSize size[2];
	const char *root;
	char xpath[0x100];

	root = path;

	while(root[0] == '/' && len > 0){

		root = &root[1];

		const char *path_end = (const char *)sceClibStrchr(root, '/');

		sceClibMemset(xpath, 0, sizeof(xpath));

		dst[0] = '/';
		dst += 1;
		len -= 1;

		if(path_end != NULL){

		/*
			if(len > (SceSize)(path_end - root))
				cpy_len = (SceSize)(path_end - root);
			else
				cpy_len = len;
		*/

			sceClibMemcpy(xpath, root, (SceSize)(path_end - root));

			res = sceHttpUriUnescape(NULL, &size[0], 0, xpath);
			if(res < 0)
				return res;

			sceHttpUriUnescape(dst, &size[1], size[0], xpath);
			if(res < 0)
				return res;

			size[1] -= 1;

			root = path_end;
			dst += size[1];
			len -= size[1];
		}else{
			sceClibMemcpy(xpath, root, sceClibStrnlen(root, len));

			res = sceHttpUriUnescape(NULL, &size[0], 0, root);
			if(res < 0)
				return res;

			res = sceHttpUriUnescape(dst, &size[1], size[0], root);
			if(res < 0)
				return res;

			root = root + sceClibStrnlen(root, 0xFF);
			len = 0;
		}

		// sceClibPrintf("xpath : %s\n", xpath);
	}

	return 0;
}

int getContentType(char *type, SceSize len, const char *path){

	const char *file_name, *path_ext;

	file_name = sceClibStrrchr(path, '/');

	if(file_name == NULL)
		file_name = sceClibStrrchr(path, ':');

	file_name += 1;

	path_ext = sceClibStrrchr(file_name, '.');
	if(path_ext != NULL){

		path_ext += 1;

		if(sceClibStrcmp(path_ext, "html") == 0){
			sceClibSnprintf(type, len, "text/html");

		}else if(sceClibStrcmp(path_ext, "png") == 0){
			sceClibSnprintf(type, len, "image/png");

		}else if(sceClibStrcmp(path_ext, "jpg") == 0){
			sceClibSnprintf(type, len, "image/jpeg");

		}else if(sceClibStrcmp(path_ext, "webp") == 0){
			sceClibSnprintf(type, len, "image/webp");

		}else if(sceClibStrcmp(path_ext, "mp3") == 0){
			sceClibSnprintf(type, len, "audio/mpeg");

		}else if(sceClibStrcmp(path_ext, "svg") == 0){
			sceClibSnprintf(type, len, "image/svg+xml");

		}else if(sceClibStrcmp(path_ext, "js") == 0){
			sceClibSnprintf(type, len, "application/x-javascript");

		}else if(sceClibStrcmp(path_ext, "php") == 0){
			sceClibSnprintf(type, len, "text/html");

		}else if(sceClibStrcmp(path_ext, "css") == 0){
			sceClibSnprintf(type, len, "text/css");

		}else if(sceClibStrcmp(path_ext, "txt") == 0){
			sceClibSnprintf(type, len, "text/plain");

		}else{
			sceClibSnprintf(type, len, "application/octet-stream");
		}
	}else{
		sceClibSnprintf(type, len, "application/octet-stream");
	}

	return 0;
}

SceUID mtxid;
SceUID semaid;
int thread_count = 0;

int working_thread = 0;

SceKernelLwMutexWork dbg_lw_mtx;

#define SCE_NET_SHUT_RD             0
#define SCE_NET_SHUT_WR             1
#define SCE_NET_SHUT_RDWR           2

int sceNetShutdown(int s, int how);

/*
todo

Connection: Keep-Alive
Content-Length: 324664
Content-Type: image/jpeg
Date: Fri, 12 Jun 2020 01:23:08 GMT
ETag: "4f438-5ca1823a:6832"
Keep-Alive: timeout=20, max=5
Last-modified: Mon, 01 Apr 2019 03:15:06 GMT
*/

const char resp_text[] =	"HTTP1.1 %d OK\r\n"
				"Connection: Keep-Alive\r\n"
				"Content-Type: %s\r\n"
				"Content-Length: %lld\r\n"
				"Keep-Alive: timeout=20, max=5\r\n"
				"Server: PS Vita\r\n"
				"\r\n";

const char resp_text_501[] =	"HTTP1.1 %d OK\r\n"
				"Content-Type: %s\r\n"
				"Content-Length: %lld\r\n"
				"Server: PS Vita\r\n"
				"\r\n";

int http_send_thread(SceSize args, void *argp){

	int res;

	int Status;
	char resp[0x400];
	char read_file_path[0x400];
	HttpSendParam http_send_param;

	sceClibMemset(read_file_path, 0, sizeof(read_file_path));
	sceClibMemcpy(&http_send_param, argp, sizeof(HttpSendParam));

	SceSize path_len;
	const char *path;

	SceUID thid = http_send_param.thid_work;
	int wsock = http_send_param.wsock;
/*
	http_send_param.memblk_uid = sceKernelAllocMemBlock("HttpArg", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, 0x2000, NULL);
	sceKernelGetMemBlockBase(http_send_param.memblk_uid, &http_send_param.arg);
*/
	http_send_param.arg = my_malloc(0x2000);

	sceNetRecv(wsock, http_send_param.arg, 0x2000, 0);

	if(sceClibStrncmp(http_send_param.arg, "GET ", 4) == 0){
/*
		int timeout;
		timeout = 10 * 1000 * 1000; // 10sec
		timeout = 3 * 1000 * 1000; // 3sec
		timeout = 500 * 1000; // 0.5sec
		sceNetSetsockopt(wsock, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout, sizeof(timeout));
*/
		sceClibPrintf("[0x%08X] NetId:0x%X\n", thid, http_send_param.wsock);

		path = (const char *)(http_send_param.arg + 4);

		const char *path_end = (const char *)sceClibStrchr(path, ' ');

		path_len = (SceSize)(path_end - path);
		if(path_len > (0x400 - 2))
			path_len = (0x400 - 2);

		sceClibMemcpy(read_file_path, path, path_len);

		// sceClibPrintf("[0x%08X] path : %s(0x%X)\n", thid, read_file_path, sceClibStrnlen(read_file_path, 0xFF));

		char out_path[0x200];

		sceClibMemset(out_path, 0, sizeof(out_path));


		getUriUnescape(out_path, sizeof(out_path) - 1, read_file_path);


		SceUID fd;
		char file_path[0x400];

		sceClibMemset(file_path, 0, sizeof(file_path));

		sceClibSnprintf(file_path, sizeof(file_path) - 1,
			"%s%s%s%s",
			BASE_PATH, (out_path[0] == '/') ? "" : "/", out_path, (out_path[sceClibStrnlen(out_path, 0x1000) - 1] == '/') ? "index.html" : ""
		);

		sceClibPrintf("[0x%08X] full : %s\n", thid, file_path);

		char content_type[0x10];

		getContentType(content_type, sizeof(content_type), file_path);

		Status = 404;

		SceIoStat stat;
		sceClibMemset(&stat, 0, sizeof(stat));

		fd = sceIoOpen(file_path, SCE_O_RDONLY, 0);
		if(fd > 0){
			Status = 200;
			sceIoGetstatByFd(fd, &stat);
		}

		sceClibPrintf("[0x%08X] sceIoOpen : 0x%X(0x%llX)\n", thid, fd, stat.st_size);

		sceClibMemset(resp, 0, sizeof(resp));
		sceClibSnprintf(resp, sizeof(resp) - 1, resp_text, Status, content_type, stat.st_size);

		sceNetSend(wsock, resp, sceClibStrnlen(resp, 0x1000), 0);

		if(fd > 0){

			// sceClibPrintf("[0x%08X] file read\n", thid);

			int bytes_read;
			void *p_send_buf, *p_send_buf_align;

#define BUFF_SIZE (0x80000)

			SceSize send_size;


			if(stat.st_size > 0x400000){
				p_send_buf = my_malloc(BUFF_SIZE + 0x200);
				send_size = BUFF_SIZE;
			}else{
				p_send_buf = my_malloc(0x400000 + 0x200);
				send_size = (SceSize)stat.st_size;
			}

			p_send_buf_align = (void *)((int)(p_send_buf + 0x1FF) & ~0x1FF);

			bytes_read = sceIoRead(fd, p_send_buf_align, send_size);
			res = 0;

			int send_count = 0;

			while(bytes_read > 0){
				res = sceNetSend(wsock, p_send_buf_align, bytes_read, 0);
				if(res != bytes_read)
					break;

				send_count++;

				bytes_read = sceIoRead(fd, p_send_buf_align, send_size);
			}

			if(bytes_read == 0) // for debug
				res = 0;

			sceIoClose(fd);

			my_free(p_send_buf);

			sceClibPrintf("[0x%08X] file read end : 0x%X/0x%X/0x%X\n", thid, bytes_read, res, send_count);
		}else{
			sceNetSend(wsock, "404 error", 9, 0);
		}

		sceNetSend(wsock, "\r\n", 2, 0);

	}else{
		sceClibPrintf("501 Not Implemented\n");
		sceClibPrintf("%s\n", http_send_param.arg);

		sceClibMemset(resp, 0, sizeof(resp));
		sceClibSnprintf(resp, sizeof(resp) - 1, resp_text_501, 501, "text/plain", 21LL);

		sceNetSend(wsock, resp, sceClibStrnlen(resp, 0x1000), 0);

		sceNetSend(wsock, "501 Not Implemented\r\n", 21, 0);
	}

	sceKernelDelayThread(1000);
	sceNetShutdown(wsock, SCE_NET_SHUT_RDWR);
	sceNetSocketClose(wsock);

	my_free(http_send_param.arg);

	// sceKernelFreeMemBlock(http_send_param.memblk_uid);

	if(http_send_param.thid != sceKernelGetThreadId()){

		sceKernelLockLwMutex(&dbg_lw_mtx, 1, NULL);

		working_thread--;
		sceClibPrintf("[0x%08X] exit, now working thread:%d\n", thid, working_thread);

		sceKernelUnlockLwMutex(&dbg_lw_mtx, 1);

		sceKernelExitDeleteThread(0);
	}

	return 0;
}

int run = 0;

int server_main_thread(SceSize args, void *argp){

	int res;

	SceNetInitParam initparam;
	SceNetInAddr vita_addr;
	SceNetCtlInfo info;
	SceNetSockaddrIn serveraddr, client;

	semaid = sceKernelCreateSema("SceHttpInitSema", 0, 1, 1, NULL);

	mtxid = sceKernelCreateMutex("SceHttpInitMutex", 0, 1, NULL);

	initparam.memory = &initparam_buf;
	initparam.size = sizeof(initparam_buf);
	initparam.flags = 0;

	res = sceNetInit(&initparam);
	if(res < 0){
		sceClibPrintf("sceNetInit : 0x%X\n", res);
		goto end;
	}

	res = sceNetCtlInit();
	if(res < 0){
		sceClibPrintf("sceNetCtlInit : 0x%X\n", res);
		goto end;
	}

	res = sceHttpInit(0x10000);
	if(res < 0){
		sceClibPrintf("sceHttpInit : 0x%X\n", res);
		goto end;
	}

	res = sceSslInit(0x800000);
	if(res < 0){
		sceClibPrintf("sceSslInit : 0x%X\n", res);
		goto end;
	}

	sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);

	sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &vita_addr);

	sceKernelCreateLwMutex(&dbg_lw_mtx, "SceDebugCountMutex", 0, 1, NULL);

	rsock = sceNetSocket("SceHttpServer", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	if(rsock < 0){
		sceClibPrintf("sceNetSocket : 0x%X\n", rsock);
		goto end;
	}

	serveraddr.sin_family = SCE_NET_AF_INET;
	serveraddr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	serveraddr.sin_port = sceNetHtons(8080);

	res = sceNetBind(rsock, (SceNetSockaddr *)&serveraddr, sizeof(serveraddr));
	if(res < 0){
		sceClibPrintf("sceNetBind : 0x%X\n", res);
		goto end;
	}

	res = sceNetListen(rsock,128);
	if(res < 0){
		sceClibPrintf("sceNetListen : 0x%X\n", res);
		goto end;
	}

	if(info.ip_address[0] == 0x17){
		sceClibPrintf("This device is not connected to the network\n");
		goto end;
	}

	printf("#access to http://%s:8080/\n\n", info.ip_address);

	while(1){
		len = sizeof(client);

		sceClibMemset(&client, 0, len);
		int wsock = sceNetAccept(rsock, (SceNetSockaddr *)&client, (unsigned int *)&len);

		if(wsock >= 0){

			HttpSendParam http_send_param;

			sceClibMemset(&http_send_param, 0, sizeof(http_send_param));

			http_send_param.wsock = wsock;
			http_send_param.thid  = sceKernelGetThreadId();

			sceNetInetNtop(SCE_NET_AF_INET, &client.sin_addr.s_addr, http_send_param.remote_ip, sizeof(http_send_param.remote_ip));


			char thread_name[0x20];

			sceClibSnprintf(thread_name, sizeof(thread_name) - 1, "SceHttpSend%X", thread_count++);

			http_send_param.thid_work = sceKernelCreateThread(thread_name, http_send_thread, 0x40, 0x10000, 0, 0, NULL);
			if(http_send_param.thid_work > 0){

				sceKernelLockLwMutex(&dbg_lw_mtx, 1, NULL);
				working_thread++;
				sceKernelUnlockLwMutex(&dbg_lw_mtx, 1);

				while(working_thread > 10){
					sceKernelDelayThread(1000);
				}

				sceKernelStartThread(http_send_param.thid_work, sizeof(HttpSendParam), &http_send_param);
			}
		}else{
			break;
		}
	}

end:
	sceNetSocketClose(rsock);
	run = 0;

	sceKernelExitDeleteThread(0);
	return 0;
}

int power_tick_thread(SceSize args, void *argp){

	while(run != 0){
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
		sceKernelDelayThread(50 * 1000); // 0.05sec
	}

	sceKernelExitDeleteThread(0);

	return 0;
}

void _start() __attribute__ ((weak, alias("module_start")));
int module_start(SceSize args, void *argp){

	SceCtrlData pad;

	SceUID thid = sceKernelCreateThread("ScePowerTickThread", power_tick_thread, 0x80, 0x1000, 0, 0, NULL);
	if(thid > 0)
		sceKernelStartThread(thid, 0, NULL);

	paf_init();

	psvDebugScreenInit();

	printf("start\n\n\n");

	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);

	thid = sceKernelCreateThread("SceHttpServerMainThread", server_main_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if(thid > 0)
		sceKernelStartThread(thid, 0, NULL);

	run = 1;

	while(run != 0){
		sceKernelDelayThread(1000 * 1000);

		sceCtrlReadBufferPositive(0, &pad, 1);

		if(pad.buttons == SCE_CTRL_START){

			run = 0;
			sceNetSocketClose(rsock);

			//sceKernelWaitThreadEnd(server_thid, NULL, NULL);
		}
	}

	sceNetCtlTerm();
	sceNetTerm();

	sceKernelExitProcess(0);
	return 0;
}
