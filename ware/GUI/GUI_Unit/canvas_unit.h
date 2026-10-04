#include "stm32f4xx.h"
#include <stdbool.h>

#ifndef __CANVAS_UNIT_H__
#define __CANVAS_UNIT_H__

void Create_Canvas_Unit(void);

void Update_Canvas_Unit(void);

void Remove_Canvas_Unit(void);

// 离开本页面前询问: 有未保存的改动时返回 false, 页面自己弹确认框
bool Canvas_Unit_Can_Exit(void);

#endif
