#ifndef __STATUS_BAR_H__
#define __STATUS_BAR_H__

#include "stm32f4xx.h"
#include "lvgl.h"

// 状态图标 Flex 容器（定义在 status_bar.c，图标等模块共用）
extern lv_obj_t *icon_flex_cont;

void Create_Status_Bar(void);
void Update_Status_Bar(void);
void Remove_Status_Bar(void);

#endif
