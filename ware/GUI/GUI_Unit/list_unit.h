#include "lvgl.h"

#ifndef __LIST_UNIT_H__
#define __LIST_UNIT_H__

// 通用文件列表组件：按后缀列出某个目录下的文件，分页浏览，点条目回调。
// 同时提供本工程统一样式的按钮 / 模态卡片 / 提示弹窗构造，供各页面复用。

// 列表配置（调用方保证在列表存活期间一直有效）
typedef struct {
	const char * dir;                    // 要列出的目录
	const char * ext;                    // 只列出此后缀的文件（含点号）
	uint16_t     page_size;              // 每页条数
	uint16_t     name_max;               // 名字（不含后缀）最大字节数
	uint32_t     file_max;               // 文件最大字节数
	uint8_t      framed;                 // 1：带黑框圆角（浮层用）；0：纯底色（页面内容区用）
	const char * empty_text;             // 列表为空时的提示
	const char * act_text;               // 右上角按钮文字；NULL 表示不建该按钮
	const char * tip_too_long;           // 名字超长时的提示
	const char * tip_too_big;            // 文件超限时的提示
	void (*on_item)(const char * name);  // 点某个条目的回调（名字已通过长度 / 大小筛选）
	void (*on_act)(void);                // 右上角按钮回调
} List_Cfg_t;

// 建一个统一样式的小按钮
lv_obj_t * List_Create_Btn(lv_obj_t * parent, const char * txt, int32_t w, int32_t h);

// 在顶层图层上建一个统一样式的模态卡片
lv_obj_t * List_Create_Dlg(int32_t y_ofs, int32_t height);

// 建列表（240x180，顶边对齐在 y=30），返回主容器；已经打开时返回当前那个
lv_obj_t * List_Create(lv_obj_t * parent, const List_Cfg_t * cfg);

// 关掉列表（顺带关掉提示弹窗）
void List_Destroy(void);

// 列表是否开着
uint8_t List_Is_Open(void);

// 通用提示弹窗：一条消息 + 「知道了」
void List_Open_Tip_Dlg(const char * msg);

// 提示弹窗是否开着
uint8_t List_Tip_Is_Open(void);

// 关掉提示弹窗
void List_Close_Tip_Dlg(void);

#endif /* __LIST_UNIT_H__ */
