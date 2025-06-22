#ifndef IMAGE_RECEIVER_H
#define IMAGE_RECEIVER_H

#include "base_types.h"
#include "flash_manager.h"

// 图像参数定义
#define IMAGE_WIDTH             400
#define IMAGE_HEIGHT            300
#define IMAGE_BW_BYTES          (IMAGE_WIDTH * IMAGE_HEIGHT / 8)  // 15000字节
#define IMAGE_RED_BYTES         (IMAGE_WIDTH * IMAGE_HEIGHT / 8)  // 15000字节
#define IMAGE_TOTAL_BYTES       (IMAGE_BW_BYTES + IMAGE_RED_BYTES) // 30000字节

// 协议参数
#define PROTOCOL_HEADER         0xAA55A55A
#define MAX_PAYLOAD_SIZE        240
#define PROTOCOL_TIMEOUT_MS     500
#define MAX_RETRIES             3

// 命令类型
#define CMD_START               0x01
#define CMD_DATA                0x02
#define CMD_END                 0x03
#define CMD_ACK                 0x04
#define CMD_NACK                0x05

// 接收状态
typedef enum {
    IMG_STATE_IDLE = 0,
    IMG_STATE_RECEIVING,
    IMG_STATE_PROCESSING,
    IMG_STATE_COMPLETE,
    IMG_STATE_ERROR
} image_state_t;

// 协议帧结构
typedef struct {
    uint32_t header;        // 帧头
    uint8_t  cmd;           // 命令类型
    uint16_t seq;           // 序列号
    uint8_t  len;           // 数据长度
    uint8_t  data[MAX_PAYLOAD_SIZE]; // 数据载荷
    uint16_t crc;           // CRC16校验
} __attribute__((packed)) protocol_frame_t;

// 图像接收器
typedef struct {
    image_state_t state;            // 当前状态
    uint16_t image_id;              // 图像ID
    uint16_t total_packets;         // 总包数
    uint16_t received_packets;      // 已接收包数
    uint16_t expected_seq;          // 期望序列号
    uint32_t received_bytes;        // 已接收字节数
    uint32_t buffer_pos;            // 缓冲区位置
    uint32_t timeout_counter;       // 超时计数器
    uint8_t  retry_count;           // 重试次数
    flash_manager_t* flash_mgr;     // Flash管理器
} image_receiver_t;

// 函数声明
void image_receiver_init(image_receiver_t* receiver, flash_manager_t* flash_mgr);
void image_receiver_process(image_receiver_t* receiver);
void image_receiver_reset(image_receiver_t* receiver);
uint16_t calculate_crc16(const uint8_t* data, uint16_t len);
uint8_t rle_compress(const uint8_t* input, uint16_t input_len, uint8_t* output, uint16_t max_output_len);

#endif // IMAGE_RECEIVER_H