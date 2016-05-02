#ifndef UTIL_H
#define UTIL_H

#include "datatypes.h"

void printAVPictureType(AVPictureType picType)
{
	if(picType == AV_PICTURE_TYPE_I) printf("I");
	else if(picType == AV_PICTURE_TYPE_P) printf("P");
	else if(picType == AV_PICTURE_TYPE_B) printf("B");
	else if(picType == AV_PICTURE_TYPE_S) printf("S");
	else if(picType == AV_PICTURE_TYPE_SI) printf("SI");
	else if(picType == AV_PICTURE_TYPE_SP) printf("SP");
	else if(picType == AV_PICTURE_TYPE_BI) printf("BI");
	else printf("NONE");
}

void printRectangle(SDL_Rect r, const char *name)
{
	printf("%s\n", name);
	printf("\t(X,Y) : (%d,%d)\n", r.x, r.y);
	printf("\t(W,H) : (%d,%d)\n", r.w, r.h);
}

void printTiming(uint64 time)
{
	if(time < 1000) printf("Finished in: [00m:00s:%dms]\n\n", time);
	else if(time < 60000)
	{
		uint64 ms = time % 1000;
		uint64 s = (time / 1000) % 60;
		char *spad = "0";
		if(s > 9) spad = "";
		printf("Finished in: [00m:%s%ds:%dms]\n\n", spad, s, ms);
	}
	else
	{
		uint64 ms = time % 1000;
		uint64 s = (time / 1000) % 60;
		uint64 m = (time / (60000)) % 60;
		char *spad = "0";
		if(s > 9) spad = "";
		char *mpad = "0";
		if(m > 9) mpad = "";
		printf("Finished in: [%s%dm:%s%ds:%dms]\n\n", mpad, m, spad, s, ms);
	}
}

void printTiming(const char *message, uint64 time)
{
	if(time < 1000) printf("Finished %s in: [00m:00s:%dms]\n\n", message, time);
	else if(time < 60000)
	{
		uint64 ms = time % 1000;
		uint64 s = (time / 1000) % 60;
		char *spad = "0";
		if(s > 9) spad = "";
		printf("Finished %s in: [00m:%s%ds:%dms]\n\n", message, spad, s, ms);
	}
	else
	{
		uint64 ms = time % 1000;
		uint64 s = (time / 1000) % 60;
		uint64 m = (time / (60000)) % 60;
		char *spad = "0";
		if(s > 9) spad = "";
		char *mpad = "0";
		if(m > 9) mpad = "";
		printf("Finished %s in: [%s%dm:%s%ds:%dms]\n\n", message, mpad, m, spad, s, ms);
	}
}

#endif