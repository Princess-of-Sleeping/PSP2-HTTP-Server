#include <psp2/net/net.h>
#include <psp2/net/netctl.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <psp2/ctrl.h>
#include <psp2/shellutil.h>
#include <psp2/sysmodule.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "graphics.h"

#define printf psvDebugScreenPrintf

#define TARGET_REQUEST_URL	0
#define TARGET_USER_AGENT	1
#define TARGET_HOST		2



static SceNetInAddr vita_addr;
static SceNetInitParam initparam;
static SceNetCtlInfo info;
static SceNetSockaddrIn serveraddr, client;
static SceCtrlData pad;

char initparam_buf[0x4000];
char request_data_buf[0x4000];
char request_path[0x200];
char read_file_path[0x200];

static int rsock, wsock;
static int len;
static int ret;



int ParseRequestData(const char *RequestData, int RequestData_size, char *output, int output_size, int target_type){

	if(RequestData == NULL){
		return -1;
	}

	char tmp[30];

	if(target_type == TARGET_REQUEST_URL){

		sprintf(tmp, "GET ");

	}else if(target_type == TARGET_USER_AGENT){

		sprintf(tmp, "User-Agent: ");

	}else if(target_type == TARGET_HOST){

		sprintf(tmp, "Host: ");

	}else{
		return -1;
	}

	const char *target = strstr(RequestData, tmp);
	int i;

	if(target == NULL){
		return -1;
	}

	for(i=0;i<strlen(target);i++){
		if((target[i+strlen(tmp)] == 0xA) || (target[i+strlen(tmp)] == 0xD)){
			break;
		}
	}

	snprintf(output, i+1, "%s", target+strlen(tmp));

	return 0;
}


void NetSend(int sock_id, char *fmt, ...){

	char buffer[0x200];
	va_list args;

	memset(buffer, 0, sizeof(buffer));

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);


	sceNetSend(sock_id, buffer, strlen(buffer), 0);

}


int get_file_path_by_request_data(char *RecvBuf, char *output){

	char request_path[0x200];
	int i;

	if(strncmp(RecvBuf, "GET ", 4) == 0){
		for(i=0;i<sizeof(request_path);i++){
			if(RecvBuf[i+5] == 0x20)break;
		}

		snprintf(request_path, i+2, "%s", RecvBuf+4);

		if(request_path[strlen(request_path)-1] == 0x2F)sprintf(request_path, "%sindex.html", request_path);

		printf("Request Path : %s\n", request_path);

		sprintf(output, "%s", request_path);

		return 0;
	}
	return -1;
}




int NetGetStatus(char *RecvBuf, char *output_path){

	char mc_path[0x200];
	SceIoStat stat;

	int Status = 200;

	get_file_path_by_request_data(RecvBuf, output_path);

	sprintf(mc_path, "ux0:www%s", output_path);

	if(sceIoGetstat(mc_path, &stat) < 0){Status = 404;}


	return Status;

}

int NetSendRequestData(int sock_id, char *RecvBuf){

	char send_buf[0x2000];
	char mc_path[0x200];
	char Content_Type[0x50];
	int Status = 200;
	int fd;
	unsigned int bytes_read;

	char user_agent[0x100];

	if(ParseRequestData(RecvBuf, strlen(RecvBuf), user_agent, sizeof(user_agent), TARGET_USER_AGENT) >= 0){
		printf("user agent : %s\n\n", user_agent);
	}


	Status = NetGetStatus(RecvBuf, request_path);

	const char *mc_path_ext = strrchr(request_path, '.');

	if (mc_path_ext) {

		if(strncmp(mc_path_ext+1, "html", 4) == 0){
			sprintf(Content_Type, "text/html");

		}else if(strncmp(mc_path_ext+1, "png", 3) == 0){
			sprintf(Content_Type, "image/png");

		}else if(strncmp(mc_path_ext+1, "jpg", 3) == 0){
			sprintf(Content_Type, "image/jpeg");

		}else if(strncmp(mc_path_ext+1, "js", 2) == 0){
			sprintf(Content_Type, "application/x-javascript");

		}else if(strncmp(mc_path_ext+1, "php", 3) == 0){
			sprintf(Content_Type, "text/html");

		}else{
			sprintf(Content_Type, "application/octet-stream");
		}

	} else {
		sprintf(Content_Type, "application/octet-stream");
	}



	sprintf(read_file_path, "ux0:www%s", request_path);

	printf("Read File Path : %s\n\n", read_file_path);

	printf("Status Code : %d\n\n", Status);

	NetSend(wsock, "HTTP1.1 %d OK\r\n", Status);

	NetSend(sock_id, "Connection: keep-alive\r\n");
	NetSend(sock_id, "Content-Type: %s;\r\n", Content_Type);

	NetSend(sock_id, "\r\n");



	if(Status < 299){

		if ((fd = sceIoOpen(read_file_path, SCE_O_RDONLY, 0777)) >= 0) {

			while ((bytes_read = sceIoRead (fd, &send_buf, sizeof(send_buf))) > 0) {
				sceNetSend(sock_id, &send_buf, bytes_read, 0);
			}

			sceIoClose(fd);
		}

	}else if(Status == 404){

		NetSend(sock_id, "404 error");

	}

	NetSend(sock_id, "\r\n");

	return Status;
}





int ErrorPrint(int ret, char *msg, ...){

	char buffer[0x200];
	va_list args;

	memset(buffer, 0, sizeof(buffer));

	va_start(args, msg);
	vsnprintf(buffer, sizeof(buffer), msg, args);
	va_end(args);

	if(ret < 0){
		printf("%s", msg);
		while (1){}
	}
	return 0;
}


int server_thread(){

	initparam.memory = &initparam_buf;
	initparam.size = sizeof(initparam_buf);
	initparam.flags = 0;

	ret = sceNetInit(&initparam);
	ErrorPrint(ret, "sceNetInit : 0x%X\n\n", ret);

	ret = sceNetCtlInit();
	ErrorPrint(ret, "sceNetCtlInit : 0x%X\n\n", ret);

	sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);

	sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &vita_addr);

	rsock = sceNetSocket("PSVita_HTTP_Server", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
	ErrorPrint(rsock, "sceNetSocket : 0x%X\n\n", rsock);

	serveraddr.sin_family = SCE_NET_AF_INET;
	serveraddr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	serveraddr.sin_port = sceNetHtons(8080);

	ret = sceNetBind(rsock, (SceNetSockaddr *)&serveraddr, sizeof(serveraddr));
	ErrorPrint(ret, "sceNetBind : 0x%X\n\n", ret);

	ret = sceNetListen(rsock, 50);
	ErrorPrint(ret, "sceNetListen : 0x%X\n\n", ret);


	if(info.ip_address[0] == 0x17){
		ErrorPrint(-1, "This device is not connected to the network\n\n");
	}


	printf("#access to http://%s:8080/\n\n", info.ip_address);


	while (1){

		len = sizeof(client);
		wsock = sceNetAccept(rsock, (SceNetSockaddr *)&client, (unsigned int *)&len);

		if(wsock >= 0){

			char remote_ip[16];
			sceNetInetNtop(SCE_NET_AF_INET, &client.sin_addr.s_addr, remote_ip, sizeof(remote_ip));


			memset(&request_data_buf, 0, 0x400);
			ret = sceNetRecv(wsock, &request_data_buf, 0x400, 0);

			if(request_data_buf[0] != 0 || ret != 0){

				//printf("sceNetRecv ret : 0x%X\n\n", ret);
				//printf("sceNetAccept : 0x%X\n\n", wsock);
				printf("client ip : %s\n\n", remote_ip);

				//printf("sceNetRecv : %s\n\n", request_data_buf);

				NetSendRequestData(wsock, request_data_buf);

				printf("\n\n");

			}



			sceNetSocketClose(wsock);

		}else{
			break;
		}

	}


	sceKernelExitDeleteThread(0);

	return 0;
}





int main(int argc, char *argv[]) {

	psvDebugScreenInit();
	sceShellUtilInitEvents(0);
	sceKernelPowerLock(0);

	printf("start\n\n");

	sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN | SCE_SHELL_UTIL_LOCK_TYPE_QUICK_MENU |
			SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU | SCE_SHELL_UTIL_LOCK_TYPE_USB_CONNECTION);

	sceSysmoduleLoadModule(1);

	int server_thid = sceKernelCreateThread("PSVita_HTTP_Server_thread", server_thread, 0x10000100, 0x10000, 0, 0, NULL);
	if(server_thid >= 0)sceKernelStartThread(server_thid, 0, NULL);

	while (1){

		sceCtrlReadBufferPositive(0, &pad, 1);

		if(pad.buttons == SCE_CTRL_START){

			sceNetSocketClose(rsock);

			//sceKernelWaitThreadEnd(server_thid, NULL, NULL);

			break;
		}
	}

	sceNetCtlTerm();
	sceNetTerm();

	sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN | SCE_SHELL_UTIL_LOCK_TYPE_QUICK_MENU |
			SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU | SCE_SHELL_UTIL_LOCK_TYPE_USB_CONNECTION);

	sceKernelPowerUnlock(0);

	sceKernelExitProcess(0);
	return 0;
}
