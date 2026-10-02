#ifndef __NOTE_UNIT_H__
#define __NOTE_UNIT_H__

#include <stdbool.h>

void Create_Note_Unit(void);
void Update_Note_Unit(void);
void Remove_Note_Unit(void);
bool Note_Unit_Can_Exit(void);

// 写文本文件 (返回 FILEOP_RES_*), 由后台文件操作任务调用
int do_write_text(const char * path, const char * text);

#endif /* __NOTE_UNIT_H__ */
