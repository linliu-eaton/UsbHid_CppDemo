#ifndef HID_DFU_H
#define HID_DFU_H
#pragma once

#include "dfu_log.h"  // only for debug

/* ********************************∫Í∂®“Â********************************* */
typedef enum {
	DFU_SUCCESS = 1,
	DFU_ILLEGAL_PARAM,
	DFU_NO_DEV_FOUND,
	DFU_INIT_FAIL,
	DFU_ERASE_FAIL,
	DFU_HEX_OPEN_FAIL,
	DFU_CHKSUM_FAIL,
	DFU_LEAVE_FAIL
} ERRS_NO;

#ifdef __cplusplus
extern "C" {
#endif
	/** @brief Burn OMRON app hex file to device flash.
		@param VID: Vendor ID
		       PID: Product ID
			   absFile: source hex file path(filenae include)
		@returns return DFU_SUCCESS on success and error code if failed.
	*/
int hidD_DfuHexBurn(unsigned int VID, unsigned int PID, const char* absFile);

int is_DFU_HID(unsigned int VID, unsigned int PID);

#ifdef __cplusplus
}
#endif

#endif // !HID_DFU_H
