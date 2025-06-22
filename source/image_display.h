#ifndef IMAGE_DISPLAY_H
#define IMAGE_DISPLAY_H

#include "base_types.h"
#include "flash_manager.h"

// 显示参数
#define DISPLAY_WIDTH           400
#define DISPLAY_HEIGHT          300
#define DISPLAY_BW_BYTES        (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)
#define DISPLAY_RED_BYTES       (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

// 显示状态
typedef enum {
    DISPLAY_STATE_IDLE = 0,
    DISPLAY_STATE_LOADING,
    DISPLAY_STATE_DISPLAYING,
    DISPLAY_STATE_COMPLETE,
    DISPLAY_STATE_ERROR
} display_state_t;

// 图像显示器
typedef struct {
    display_state_t state;
    uint16_t image_id;
    uint32_t loaded_bytes;
    flash_manager_t* flash_mgr;
} image_display_t;

// 函数声明
void image_display_init(image_display_t* display, flash_manager_t* flash_mgr);
uint8_t image_display_show(image_display_t* display, uint16_t image_id);
void image_display_process(image_display_t* display);
void image_display_reset(image_display_t* display);

#endif // IMAGE_DISPLAY_H