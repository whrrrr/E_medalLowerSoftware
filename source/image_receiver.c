#include "image_receiver.h"
#include "queue.h"
#include "uart_interface.h"
#include <string.h>

// 外部变量声明
extern Queue lpUartRecdata;
extern uint8_t g_flash_buffer[FLASH_PAGE_SIZE];

// 额外的256字节缓冲区用于数据累积
static uint8_t g_image_buffer[FLASH_PAGE_SIZE];

// 协议解析缓冲区
static uint8_t g_protocol_buffer[sizeof(protocol_frame_t)];
static uint16_t g_protocol_pos = 0;

/**
 * @brief 计算CRC16校验码
 */
uint16_t calculate_crc16(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    uint16_t i, j;
    
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief RLE压缩
 */
uint8_t rle_compress(const uint8_t* input, uint16_t input_len, uint8_t* output, uint16_t max_output_len)
{
    uint16_t in_pos = 0, out_pos = 0;
    
    while (in_pos < input_len && out_pos < max_output_len - 1) {
        uint8_t current = input[in_pos];
        uint8_t count = 1;
        
        // 计算重复次数
        while (in_pos + count < input_len && input[in_pos + count] == current && count < 127) {
            count++;
        }
        
        if (count > 2 || current == 0x80) {
            // 使用RLE编码
            output[out_pos++] = 0x80 | count;
            output[out_pos++] = current;
        } else {
            // 直接存储
            for (uint8_t i = 0; i < count; i++) {
                if (out_pos >= max_output_len) break;
                output[out_pos++] = current;
            }
        }
        in_pos += count;
    }
    
    return (out_pos < max_output_len) ? 1 : 0;
}

/**
 * @brief 发送ACK/NACK
 */
static void send_response(uint8_t cmd, uint16_t seq)
{
    protocol_frame_t frame;
    frame.header = PROTOCOL_HEADER;
    frame.cmd = cmd;
    frame.seq = seq;
    frame.len = 0;
    frame.crc = calculate_crc16((uint8_t*)&frame, sizeof(frame) - 2);
    
    // 通过LPUART发送响应
    uint8_t* ptr = (uint8_t*)&frame;
    for (uint16_t i = 0; i < sizeof(frame); i++) {
        // 这里应该调用LPUART发送函数，暂时省略具体实现
    }
}

/**
 * @brief 处理开始帧
 */
static void handle_start_frame(image_receiver_t* receiver, const protocol_frame_t* frame)
{
    if (frame->len >= 6) {
        receiver->image_id = (frame->data[1] << 8) | frame->data[0];
        receiver->total_packets = (frame->data[3] << 8) | frame->data[2];
        receiver->received_packets = 0;
        receiver->expected_seq = 0;
        receiver->received_bytes = 0;
        receiver->buffer_pos = 0;
        receiver->state = IMG_STATE_RECEIVING;
        
        send_response(CMD_ACK, frame->seq);
    } else {
        send_response(CMD_NACK, frame->seq);
    }
}

/**
 * @brief 处理数据帧
 */
static void handle_data_frame(image_receiver_t* receiver, const protocol_frame_t* frame)
{
    if (frame->seq != receiver->expected_seq) {
        send_response(CMD_NACK, frame->seq);
        return;
    }
    
    // 将数据复制到缓冲区
    uint16_t copy_len = frame->len;
    if (receiver->buffer_pos + copy_len > FLASH_PAGE_SIZE) {
        copy_len = FLASH_PAGE_SIZE - receiver->buffer_pos;
    }
    
    memcpy(&g_image_buffer[receiver->buffer_pos], frame->data, copy_len);
    receiver->buffer_pos += copy_len;
    receiver->received_bytes += copy_len;
    
    // 如果缓冲区满了，写入Flash
    if (receiver->buffer_pos >= FLASH_PAGE_SIZE) {
        uint16_t data_id = receiver->image_id * 1000 + (receiver->received_bytes / FLASH_PAGE_SIZE);
        
        if (flash_write_data(receiver->flash_mgr, data_id, g_image_buffer, FLASH_PAGE_SIZE) == FLASH_OK) {
            receiver->buffer_pos = 0;
            memset(g_image_buffer, 0, FLASH_PAGE_SIZE);
        } else {
            receiver->state = IMG_STATE_ERROR;
            send_response(CMD_NACK, frame->seq);
            return;
        }
    }
    
    receiver->received_packets++;
    receiver->expected_seq++;
    send_response(CMD_ACK, frame->seq);
}

/**
 * @brief 处理结束帧
 */
static void handle_end_frame(image_receiver_t* receiver, const protocol_frame_t* frame)
{
    // 写入剩余数据
    if (receiver->buffer_pos > 0) {
        uint16_t data_id = receiver->image_id * 1000 + (receiver->received_bytes / FLASH_PAGE_SIZE);
        
        if (flash_write_data(receiver->flash_mgr, data_id, g_image_buffer, receiver->buffer_pos) == FLASH_OK) {
            receiver->state = IMG_STATE_COMPLETE;
            send_response(CMD_ACK, frame->seq);
        } else {
            receiver->state = IMG_STATE_ERROR;
            send_response(CMD_NACK, frame->seq);
        }
    } else {
        receiver->state = IMG_STATE_COMPLETE;
        send_response(CMD_ACK, frame->seq);
    }
}

/**
 * @brief 解析协议帧
 */
static uint8_t parse_frame(image_receiver_t* receiver)
{
    if (g_protocol_pos < sizeof(protocol_frame_t)) {
        return 0; // 数据不完整
    }
    
    protocol_frame_t* frame = (protocol_frame_t*)g_protocol_buffer;
    
    // 检查帧头
    if (frame->header != PROTOCOL_HEADER) {
        g_protocol_pos = 0;
        return 0;
    }
    
    // 检查CRC
    uint16_t calc_crc = calculate_crc16(g_protocol_buffer, sizeof(protocol_frame_t) - 2);
    if (calc_crc != frame->crc) {
        g_protocol_pos = 0;
        return 0;
    }
    
    // 处理不同类型的帧
    switch (frame->cmd) {
        case CMD_START:
            handle_start_frame(receiver, frame);
            break;
        case CMD_DATA:
            handle_data_frame(receiver, frame);
            break;
        case CMD_END:
            handle_end_frame(receiver, frame);
            break;
        default:
            break;
    }
    
    g_protocol_pos = 0;
    return 1;
}

/**
 * @brief 初始化图像接收器
 */
void image_receiver_init(image_receiver_t* receiver, flash_manager_t* flash_mgr)
{
    memset(receiver, 0, sizeof(image_receiver_t));
    receiver->state = IMG_STATE_IDLE;
    receiver->flash_mgr = flash_mgr;
    g_protocol_pos = 0;
    memset(g_image_buffer, 0, FLASH_PAGE_SIZE);
}

/**
 * @brief 重置接收器
 */
void image_receiver_reset(image_receiver_t* receiver)
{
    receiver->state = IMG_STATE_IDLE;
    receiver->received_packets = 0;
    receiver->expected_seq = 0;
    receiver->received_bytes = 0;
    receiver->buffer_pos = 0;
    receiver->timeout_counter = 0;
    receiver->retry_count = 0;
    g_protocol_pos = 0;
    memset(g_image_buffer, 0, FLASH_PAGE_SIZE);
}

/**
 * @brief 处理图像接收
 */
void image_receiver_process(image_receiver_t* receiver)
{
    uint8_t data;
    
    // 从队列中读取数据
    while (Queue_Dequeue(&lpUartRecdata, &data)) {
        // 将数据添加到协议缓冲区
        if (g_protocol_pos < sizeof(protocol_frame_t)) {
            g_protocol_buffer[g_protocol_pos++] = data;
            
            // 尝试解析帧
            if (parse_frame(receiver)) {
                receiver->timeout_counter = 0;
            }
        } else {
            // 缓冲区溢出，重置
            g_protocol_pos = 0;
        }
    }
    
    // 超时处理
    if (receiver->state == IMG_STATE_RECEIVING) {
        receiver->timeout_counter++;
        if (receiver->timeout_counter > PROTOCOL_TIMEOUT_MS) {
            receiver->retry_count++;
            if (receiver->retry_count >= MAX_RETRIES) {
                receiver->state = IMG_STATE_ERROR;
            } else {
                // 请求重传
                receiver->timeout_counter = 0;
            }
        }
    }
}