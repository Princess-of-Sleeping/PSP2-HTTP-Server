#include <psp2/ctrl.h>

int get_key(int type) {


	SceCtrlData pad;

	if(type == 0){

		while (1) {
			sceCtrlPeekBufferPositive(0, &pad, 1);

				if (pad.buttons != 0)
					return pad.buttons;
		}

	}else{

		while (1) {
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if(pad.buttons == 0){
				break;
			}
		}

	}

	return 0;

}


void press_next(void) {

	get_key(1);
	get_key(0);
	get_key(1);

}


void press_exit(void) {

	get_key(1);
	get_key(0);
	get_key(1);

	sceKernelExitProcess(0);

}