#pragma once
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

	void open_log();
	FILE* GetLogHandler();

#ifdef __cplusplus
}
#endif

#define log_info(fmt, ...) do{\
						if ( NULL != GetLogHandler() ){\
							fprintf(GetLogHandler(), fmt, __VA_ARGS__);\
							fflush(GetLogHandler());\
						}\
					}while(0)