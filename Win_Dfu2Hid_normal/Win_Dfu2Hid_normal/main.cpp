#include<stdio.h>
#include<time.h>
#include "./Include/hid_dfu.h"


int main(void)
{
	log_info("--------------------------Test Begin------------------------\n");
	open_log();  // only for debug
	time_t start = time(NULL);

	int ret = hidD_DfuHexBurn(0x0590, 0xFFFF, "BRN_USB_APP.hex");
	log_info("main, ret = %d\n", ret);

	time_t end = time(NULL);
	log_info("time costs for 1 time burn: %lld second(s)\n", end - start);

	return 0;
}