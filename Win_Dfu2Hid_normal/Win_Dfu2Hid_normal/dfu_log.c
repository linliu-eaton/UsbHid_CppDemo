/* -------------------only for debug------------------ */
#include <stdio.h>

static FILE *pFD = NULL;

FILE* GetLogHandler()
{
	return pFD;
}

#define log_info(fmt, ...) do{\
						if ( NULL != GetLogHandler() ){\
							fprintf(GetLogHandler(), fmt, __VA_ARGS__);\
							fflush(GetLogHandler());\
						}\
					}while(0)

void open_log() {
	if (pFD == NULL) {
		fopen_s(&pFD, "debug12.log", "wt");
		log_info("----open debug.log OK----\n");
	}
}
/* -------------------only for debug------------------ */