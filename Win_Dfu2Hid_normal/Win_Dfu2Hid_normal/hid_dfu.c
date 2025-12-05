#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "hidsdi.h"
#include "setupapi.h"
#include "./Include/hid_dfu.h"

/* ********************************宏定义********************************* */
#define FALSE 0
#define TRUE 1

#define WRITE_MSG_LEN  965
#define HID_BUF_LEN  1024

#define DFU_ERROR_NONE      0x00
#define DFU_ERROR_UNKNOWN   0x0E

#define DFU_STATE_IDLE                 2
#define DFU_STATE_DNLOAD_SYNC          3
#define DFU_STATE_DNLOAD_BUSY          4
#define DFU_STATE_DNLOAD_IDLE          5
#define DFU_STATE_MANIFEST_SYNC        6
#define DFU_STATE_MANIFEST             7
#define DFU_STATE_ERROR                10

typedef enum {
	CMD_ERASE = 1,
	CMD_LEAVE = 2,
	CMD_CLR = 3,
	CMD_ABORT = 4,
	CMD_WRITE = 5,
	CMD_GET_STATUS = 6,
	CMD_GET_STATE = 7
}DFU_CMD_ID;

typedef struct {
	unsigned char colon;
	unsigned int length;
	unsigned int address;
	unsigned int type;
	unsigned int chksum;
} HEX_FORMAT;

typedef struct {
	UINT8 status;
	UINT8 pollTimeout1;
	UINT8 pollTimeout2;
	UINT8 pollTimeout3;
	UINT8 state;
	UINT8 iString;
} STATUS_RSP;

/* ********************************全局变量********************************* */
static HANDLE g_hReadHandle = NULL;
static HANDLE g_hWriteHandle = NULL;
static PSP_DEVICE_INTERFACE_DETAIL_DATA g_sDetailData = NULL;
static unsigned char g_aReadBuffer[HID_BUF_LEN] = { 0 };
static unsigned char g_aWriteBuffer[HID_BUF_LEN] = { 0 };

/* ********************************函数声明********************************* */

/* ************************************************************************* */

static int CreateReadHandle()
{
	if (g_sDetailData == NULL) {
		return FALSE;
	}

	g_hReadHandle = CreateFile(g_sDetailData->DevicePath,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		(LPSECURITY_ATTRIBUTES)NULL,
		OPEN_EXISTING,
		0,
		NULL);
	return g_hReadHandle == NULL ? FALSE : TRUE;
}

static int CreateWriteHandle()
{
	if (g_sDetailData == NULL) {
		return FALSE;
	}

	g_hWriteHandle = CreateFile(g_sDetailData->DevicePath,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		(LPSECURITY_ATTRIBUTES)NULL,
		OPEN_EXISTING,
		0,
		NULL);
	return g_hWriteHandle == NULL ? FALSE : TRUE;
}

static int HidDfuInit()
{
	memset(g_aReadBuffer, 0, HID_BUF_LEN);
	memset(g_aWriteBuffer, 0, HID_BUF_LEN);

	return CreateWriteHandle() && CreateReadHandle();
}

static void HidDfuDeinit()
{
	if (g_sDetailData != NULL) {
		free(g_sDetailData);
		g_sDetailData = NULL;
	}
	if (g_hReadHandle != NULL) {
		CloseHandle(g_hReadHandle);
		g_hReadHandle = NULL;
	}
	if (g_hWriteHandle != NULL) {
		CloseHandle(g_hWriteHandle);
		g_hWriteHandle = NULL;
	}
}

/*find target HID device by VID & PID */
static int OpenHIDDevice(unsigned int VID, unsigned int PID)
{
	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);

	HANDLE hDevInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
	SP_INTERFACE_DEVICE_DATA devInfoData;
	devInfoData.cbSize = sizeof(devInfoData);

	int memberInedx = 0;
	int devDetected = FALSE;
	int bFinishFind = FALSE;
	while (!bFinishFind) {
		int result = SetupDiEnumDeviceInterfaces(hDevInfo, 0, &hidGuid, memberInedx, &devInfoData);
		memberInedx++;
		if (result) {
			ULONG length = 0;
			result = SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInfoData, NULL, 0, &length, NULL);
			PSP_DEVICE_INTERFACE_DETAIL_DATA detailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(length);
			if (detailData == NULL) {
				log_info("%s|%d: malloc detail data fail.\n", __FUNCTION__, __LINE__);
				return FALSE;
			}
			detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
			ULONG required;
			result = SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInfoData, detailData, length, &required, NULL);
			HANDLE DeviceHandle = CreateFile(detailData->DevicePath,
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				(LPSECURITY_ATTRIBUTES)NULL,
				OPEN_EXISTING,
				0,
				NULL);
			if (INVALID_HANDLE_VALUE == DeviceHandle) {
				log_info("%s|%d: create file fail, err = %d.\n", __FUNCTION__, __LINE__, GetLastError());
				free(detailData);
				continue;
			}

			HIDD_ATTRIBUTES  Attributes;
			Attributes.Size = sizeof(Attributes);
			(void)HidD_GetAttributes(DeviceHandle, &Attributes);
			log_info("%s|%d: DEV INFO, VendorID: 0x%04X, ProductID: 0x%04X\n", __FUNCTION__, __LINE__, Attributes.VendorID, Attributes.ProductID);
			if ((Attributes.VendorID == VID) && (Attributes.ProductID == PID)) {
				devDetected = TRUE;
				g_sDetailData = detailData;
				CloseHandle(DeviceHandle);
				DeviceHandle = INVALID_HANDLE_VALUE;
				log_info("%s|%d: SELECTED DEV, VendorID: 0x%04X, ProductID: 0x%04X, DevPath: %s\n", __FUNCTION__, __LINE__,
					Attributes.VendorID, Attributes.ProductID, detailData->DevicePath);
				break;
			}
			free(detailData);
			CloseHandle(DeviceHandle);
			DeviceHandle = INVALID_HANDLE_VALUE;
		}
		else {
			if (ERROR_NO_MORE_ITEMS == GetLastError()) {
				bFinishFind = TRUE;
				devDetected = FALSE;
			}
		}
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);
	return devDetected;
}

static int CmdGetState(int *state)
{
	memset(g_aReadBuffer, 0, HID_BUF_LEN);
	g_aReadBuffer[0] = CMD_GET_STATE;
	int ret = HidD_GetInputReport(g_hReadHandle, g_aReadBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	*state = g_aReadBuffer[1];
	return TRUE;
}

static int CmdGetStatus(STATUS_RSP* statusRsp)
{
	if (statusRsp == NULL) {
		return FALSE;
	}

	memset(g_aReadBuffer, 0, HID_BUF_LEN);
	g_aReadBuffer[0] = CMD_GET_STATUS;
	int ret = HidD_GetInputReport(g_hReadHandle, g_aReadBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	memcpy(statusRsp, &(g_aReadBuffer[1]), sizeof(STATUS_RSP));
	return TRUE;
}

static int CmdClrStatus()
{
	memset(g_aWriteBuffer, 0, HID_BUF_LEN);
	g_aWriteBuffer[0] = CMD_CLR;
	int ret = HidD_SetOutputReport(g_hWriteHandle, g_aWriteBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	return TRUE;
}

static int CmdAbort()
{
	memset(g_aWriteBuffer, 0, HID_BUF_LEN);
	g_aWriteBuffer[0] = CMD_ABORT;
	int ret = HidD_SetOutputReport(g_hWriteHandle, g_aWriteBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	return TRUE;
}

static int CmdLeave()
{
	memset(g_aWriteBuffer, 0, HID_BUF_LEN);
	g_aWriteBuffer[0] = CMD_LEAVE;
	int ret = HidD_SetOutputReport(g_hWriteHandle, g_aWriteBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	return TRUE;
}

static int CmdErase()
{
	memset(g_aWriteBuffer, 0, HID_BUF_LEN);
	g_aWriteBuffer[0] = CMD_ERASE;
	int ret = HidD_SetOutputReport(g_hWriteHandle, g_aWriteBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	return TRUE;
}

static int CmdWrite()
{
	int ret = HidD_SetOutputReport(g_hWriteHandle, g_aWriteBuffer, HID_BUF_LEN);
	if (!ret) {
		return FALSE;
	}

	return TRUE;
}

static int DfuLeave()
{
	if (!CmdLeave()) {
		log_info("%s|%d: send LEAVE fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
		return FALSE;
	}

	STATUS_RSP statusRsp = { 0 };
	CmdGetStatus(&statusRsp);
	return TRUE;
}

static int DfuErase()
{
	if (!CmdErase()) {
		log_info("%s|%d: send ERASE fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
		return FALSE;
	}

	STATUS_RSP statusRsp = { 0 };
	const unsigned int tryTimes = 3;
	for (unsigned int i = 0; i < tryTimes; i++) {
		// 1st get status
		if (!CmdGetStatus(&statusRsp)) {
			log_info("%s|%d: GET STATUS fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
			return FALSE;
		}
		if (statusRsp.status == DFU_ERROR_UNKNOWN) {
			if (!CmdClrStatus()) {
				log_info("%s|%d: CLR STATUS fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
				return FALSE;
			}
			if (!CmdErase()) {
				log_info("%s|%d: 2nd ERASE fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
				return FALSE;
			}
			continue;
		}
		if (statusRsp.status == DFU_ERROR_NONE && statusRsp.state == DFU_STATE_DNLOAD_BUSY) {
			Sleep(statusRsp.pollTimeout1);
		}

		// 2nd get status
		if (!CmdGetStatus(&statusRsp)) {
			log_info("%s|%d: GET STATUS fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
			return FALSE;
		}
		if (statusRsp.status == DFU_ERROR_UNKNOWN) {
			if (!CmdClrStatus()) {
				log_info("%s|%d: CLR STATUS fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
				return FALSE;
			}
			if (!CmdErase()) {
				log_info("%s|%d: 2nd ERASE fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
				return FALSE;
			}
			continue;
		}
		if (statusRsp.state == DFU_STATE_DNLOAD_IDLE) {
			return TRUE;
		}
	}
	return FALSE;
}

static void printWriteMsg()
{
	static int index = 0;
	int msgLen = (g_aWriteBuffer[1] << 8) + g_aWriteBuffer[2];
	msgLen = (msgLen + 5) < 1024 ? (msgLen + 5) : 1024;
	log_info("write msg(%d):\n", index);
	for (int i = 0; i < msgLen; i++) {
		log_info("%02x ", g_aWriteBuffer[i]);
	}
	log_info("\n");
	index++;
}

static int DfuWrite()
{
	printWriteMsg();
	if (!CmdWrite()) {
		log_info("%s|%d: send WRITE fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
		return FALSE;
	}

	STATUS_RSP statusRsp = { 0 };
	if (!CmdGetStatus(&statusRsp)) {
		log_info("%s|%d: GET STATUS fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
		return FALSE;
	}
	if (statusRsp.status == DFU_ERROR_NONE && statusRsp.state == DFU_STATE_DNLOAD_BUSY) {
		Sleep(statusRsp.pollTimeout1);
	}
	else {
		return FALSE;
	}

	if (!CmdGetStatus(&statusRsp)) {
		log_info("%s|%d: 2nd GET STATUS fail, err=%d\n", __FUNCTION__, __LINE__, GetLastError());
		return FALSE;
	}
	if (statusRsp.state == DFU_STATE_DNLOAD_IDLE) {
		return TRUE;
	}
	return FALSE;
}

static int BurnHexFile(const char* absFile)
{
	if (absFile == NULL) {
		return DFU_ILLEGAL_PARAM;
	}

	FILE* fp = NULL;
	if (fopen_s(&fp, absFile, "r") != 0) {
		return DFU_HEX_OPEN_FAIL;
	}

	HEX_FORMAT hexFmt;
	unsigned int index = 5;
	UINT16 block_num = 0;
	while (!feof(fp)) {
		memset(&hexFmt, 0, sizeof(HEX_FORMAT));
		fscanf_s(fp, "%c", &(hexFmt.colon));
		if (hexFmt.colon == ':') {
			unsigned long checksum = 0;
			fscanf_s(fp, "%2x", &(hexFmt.length));
			checksum += hexFmt.length;
			fscanf_s(fp, "%4x", &(hexFmt.address));
			checksum += ((hexFmt.address >> 8) + (hexFmt.address % 256));
			fscanf_s(fp, "%2x", &(hexFmt.type));
			checksum += hexFmt.type;

			for (unsigned int i = 0; i < hexFmt.length; i++) {
				unsigned int tmp = 0;
				fscanf_s(fp, "%2x", &tmp);
				checksum += tmp;
				if (hexFmt.type == 0x00) {
					g_aWriteBuffer[index] = tmp;
					index++;
					if (index >= WRITE_MSG_LEN) {
						g_aWriteBuffer[0] = 0x05;
						g_aWriteBuffer[1] = (WRITE_MSG_LEN - 5) >> 8;
						g_aWriteBuffer[2] = (WRITE_MSG_LEN - 5) % 256;
						g_aWriteBuffer[3] = block_num >> 8;
						g_aWriteBuffer[4] = block_num % 256;
						index = 5;
						block_num++;
						DfuWrite();
					}
				}
			}
			fscanf_s(fp, "%2x", &(hexFmt.chksum));
			checksum = checksum % 256;
			if (((hexFmt.chksum + checksum) % 256) != 0) {
				log_info("%s|%d: hex check sum fail at address: 0x%04X\n", __FUNCTION__, __LINE__, hexFmt.address);
				fclose(fp);
				return DFU_CHKSUM_FAIL;
			}
		}
	}
	//最后一帧不足956
	if (index > 5 && index <= WRITE_MSG_LEN) {
		g_aWriteBuffer[0] = 0x05;
		g_aWriteBuffer[1] = (index - 5) >> 8;
		g_aWriteBuffer[2] = (index - 5) % 256;
		g_aWriteBuffer[3] = block_num >> 8;
		g_aWriteBuffer[4] = block_num % 256;
		DfuWrite();
	}
	fclose(fp);
	return DFU_SUCCESS;
}

int hidD_DfuHexBurn(unsigned int VID, unsigned int PID, const char* absFile)
{
	if (OpenHIDDevice(VID, PID)) {
		log_info("%s|%d:Open HID Device OK, continue...\n", __FUNCTION__, __LINE__);
	}
	else {
		log_info("%s|%d: Open HID Device failed, exit!\n", __FUNCTION__, __LINE__);
		return DFU_NO_DEV_FOUND;
	}

	if (!HidDfuInit()) {
		return DFU_INIT_FAIL;
	}

	// 起步先Abort一把确保状态机在初始状态
	CmdAbort();

	if (!DfuErase()) {
		return DFU_ERASE_FAIL;
	}
	log_info("%s|%d: ERASE OK...\n", __FUNCTION__, __LINE__);

	int ret = BurnHexFile(absFile);
	if (ret != DFU_SUCCESS) {
		return ret;
	}
	log_info("%s|%d: WRITE OK...\n", __FUNCTION__, __LINE__);

	if (!DfuLeave()) {
		log_info("%s|%d: LEAVE OK...\n", __FUNCTION__, __LINE__);
		return DFU_LEAVE_FAIL;
	}

	HidDfuDeinit();
	log_info("%s|%d: Firmware Upgrade Success!\n", __FUNCTION__, __LINE__);

	return DFU_SUCCESS;
}

int is_DFU_HID(unsigned int VID, unsigned int PID)
{
	int ret = OpenHIDDevice(VID, PID);
	if (g_sDetailData != NULL) {
		free(g_sDetailData);
		g_sDetailData = NULL;
	}
	return ret;
}