#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
图像传输测试程序 - 上位机端
用于测试下位机的图像接收功能
"""

import serial
import struct
import time
import random
from typing import List, Tuple

# 协议常量
PROTOCOL_HEADER = 0xAA55A55A
MAX_PAYLOAD_SIZE = 240
CMD_START = 0x01
CMD_DATA = 0x02
CMD_END = 0x03
CMD_ACK = 0x04
CMD_NACK = 0x05

# 图像参数
IMAGE_WIDTH = 400
IMAGE_HEIGHT = 300
IMAGE_BW_BYTES = IMAGE_WIDTH * IMAGE_HEIGHT // 8  # 15000字节
IMAGE_RED_BYTES = IMAGE_WIDTH * IMAGE_HEIGHT // 8  # 15000字节
IMAGE_TOTAL_BYTES = IMAGE_BW_BYTES + IMAGE_RED_BYTES  # 30000字节

class ImageSender:
    def __init__(self, port: str, baudrate: int = 115200):
        self.serial = serial.Serial(port, baudrate, timeout=1)
        self.seq = 0
        
    def crc16(self, data: bytes) -> int:
        """计算CRC16校验码"""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc & 0xFFFF
    
    def create_frame(self, cmd: int, data: bytes = b'') -> bytes:
        """创建协议帧"""
        frame_data = struct.pack('<LBH', PROTOCOL_HEADER, cmd, self.seq)
        frame_data += struct.pack('B', len(data))
        frame_data += data
        
        # 填充到最大载荷大小
        if len(data) < MAX_PAYLOAD_SIZE:
            frame_data += b'\x00' * (MAX_PAYLOAD_SIZE - len(data))
        
        # 计算CRC
        crc = self.crc16(frame_data)
        frame_data += struct.pack('<H', crc)
        
        return frame_data
    
    def wait_for_ack(self, timeout: float = 2.0) -> bool:
        """等待ACK响应"""
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.serial.in_waiting >= 11:  # 最小帧长度
                response = self.serial.read(11)
                if len(response) >= 11:
                    header, cmd, seq = struct.unpack('<LBH', response[:7])
                    if header == PROTOCOL_HEADER and cmd == CMD_ACK and seq == self.seq:
                        return True
                    elif header == PROTOCOL_HEADER and cmd == CMD_NACK:
                        print(f"收到NACK，序列号: {seq}")
                        return False
            time.sleep(0.01)
        return False
    
    def generate_test_image(self) -> bytes:
        """生成测试图像数据"""
        # 生成黑白图像数据（简单的棋盘格模式）
        bw_data = bytearray(IMAGE_BW_BYTES)
        for i in range(IMAGE_BW_BYTES):
            # 创建棋盘格模式
            byte_pos = i
            row = (byte_pos * 8) // IMAGE_WIDTH
            col = (byte_pos * 8) % IMAGE_WIDTH
            if (row // 20 + col // 20) % 2 == 0:
                bw_data[i] = 0xFF  # 白色
            else:
                bw_data[i] = 0x00  # 黑色
        
        # 生成红白图像数据（简单的条纹模式）
        red_data = bytearray(IMAGE_RED_BYTES)
        for i in range(IMAGE_RED_BYTES):
            # 创建水平条纹
            byte_pos = i
            row = (byte_pos * 8) // IMAGE_WIDTH
            if row % 40 < 20:
                red_data[i] = 0xFF  # 红色
            else:
                red_data[i] = 0x00  # 白色
        
        return bytes(bw_data + red_data)
    
    def send_image(self, image_id: int) -> bool:
        """发送图像数据"""
        print(f"开始发送图像 ID: {image_id}")
        
        # 生成测试图像
        image_data = self.generate_test_image()
        total_bytes = len(image_data)
        total_packets = (total_bytes + MAX_PAYLOAD_SIZE - 1) // MAX_PAYLOAD_SIZE
        
        print(f"图像大小: {total_bytes} 字节，总包数: {total_packets}")
        
        # 发送开始帧
        self.seq = 0
        start_data = struct.pack('<HH', image_id, total_packets)
        start_frame = self.create_frame(CMD_START, start_data)
        
        print("发送开始帧...")
        self.serial.write(start_frame)
        
        if not self.wait_for_ack():
            print("开始帧未收到ACK")
            return False
        
        print("开始帧ACK已收到")
        
        # 发送数据帧
        sent_bytes = 0
        for packet_num in range(total_packets):
            self.seq += 1
            
            # 计算当前包的数据
            start_pos = packet_num * MAX_PAYLOAD_SIZE
            end_pos = min(start_pos + MAX_PAYLOAD_SIZE, total_bytes)
            packet_data = image_data[start_pos:end_pos]
            
            # 发送数据帧
            data_frame = self.create_frame(CMD_DATA, packet_data)
            self.serial.write(data_frame)
            
            # 等待ACK
            retry_count = 0
            while retry_count < 3:
                if self.wait_for_ack(1.0):
                    break
                retry_count += 1
                print(f"包 {packet_num + 1} 重试 {retry_count}")
                self.serial.write(data_frame)
            
            if retry_count >= 3:
                print(f"包 {packet_num + 1} 发送失败")
                return False
            
            sent_bytes += len(packet_data)
            if (packet_num + 1) % 10 == 0:
                print(f"已发送 {packet_num + 1}/{total_packets} 包，{sent_bytes}/{total_bytes} 字节")
        
        # 发送结束帧
        self.seq += 1
        end_frame = self.create_frame(CMD_END)
        
        print("发送结束帧...")
        self.serial.write(end_frame)
        
        if not self.wait_for_ack():
            print("结束帧未收到ACK")
            return False
        
        print("图像发送完成！")
        return True
    
    def close(self):
        """关闭串口"""
        self.serial.close()

def main():
    # 配置串口（根据实际情况修改）
    PORT = "COM3"  # Windows
    # PORT = "/dev/ttyUSB0"  # Linux
    
    try:
        sender = ImageSender(PORT)
        print(f"已连接到 {PORT}")
        
        # 发送测试图像
        image_id = 1
        success = sender.send_image(image_id)
        
        if success:
            print("图像传输成功！")
        else:
            print("图像传输失败！")
        
        sender.close()
        
    except serial.SerialException as e:
        print(f"串口错误: {e}")
    except KeyboardInterrupt:
        print("\n用户中断")
    except Exception as e:
        print(f"错误: {e}")

if __name__ == "__main__":
    main()