#ifndef __MUSIC_H__
#define __MUSIC_H__

#include "stm32f4xx.h"

//MUSIC信息结构体
typedef __packed struct
{
	char *music_name;               //音乐名
	char *artist_name;              //歌手名
	uint8_t file_format;            //音乐文件格式

	uint16_t file_date;
	uint16_t file_time;

	uint16_t total_sec;
	uint16_t current_sec;

	uint32_t bitrate;
	uint32_t samplerate;
	uint8_t bit_depth;
} __musicinfo;

extern __musicinfo music_info;

#endif
