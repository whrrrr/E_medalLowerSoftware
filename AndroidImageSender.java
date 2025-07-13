/**
 * Android上位机图像传输示例代码
 * 实现与HC32L110下位机的蓝牙通信协议
 * 
 * 使用方法:
 * 1. 确保已获得蓝牙权限
 * 2. 连接到下位机蓝牙设备
 * 3. 调用sendImage()方法传输图像
 */

package com.example.imageTransfer;

import android.bluetooth.BluetoothSocket;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;

public class AndroidImageSender {
    private static final String TAG = "ImageSender";
    
    // 协议常量
    private static final int PROTOCOL_MAGIC_HOST = 0xA5A5;
    private static final int PROTOCOL_MAGIC_MCU = 0x5A5A;
    private static final long PROTOCOL_END_HOST = 0xA5A5AFAFL;
    private static final long PROTOCOL_END_MCU = 0x5A5A5F5FL;
    
    // 命令类型
    private static final byte CMD_IMAGE_TRANSFER = (byte)0xC0;
    private static final byte CMD_IMAGE_DATA = (byte)0xD0;
    private static final byte CMD_TRANSFER_END = (byte)0xC1;
    
    // 颜色类型
    private static final byte COLOR_TYPE_BW = 0x00;
    private static final byte COLOR_TYPE_RED = 0x10;
    
    // 状态码
    private static final byte MCU_STATUS_OK = 0x01;
    private static final byte MCU_STATUS_BUSY = 0x02;
    private static final byte MCU_STATUS_ERROR = (byte)0xFF;
    
    private static final byte DATA_STATUS_OK = 0x00;
    private static final byte DATA_STATUS_CRC_ERROR = 0x10;
    private static final byte DATA_STATUS_FRAME_MISSING = 0x20;
    private static final byte DATA_STATUS_TIMEOUT = 0x30;
    
    // 图像参数
    private static final int IMAGE_WIDTH = 400;
    private static final int IMAGE_HEIGHT = 300;
    private static final int IMAGE_SIZE = (IMAGE_WIDTH * IMAGE_HEIGHT) / 8;  // 15000字节
    private static final int IMAGE_PAGES_PER_COLOR = 61;
    private static final int IMAGE_DATA_PER_PAGE = 248;
    private static final int IMAGE_LAST_PAGE_DATA_SIZE = 120;
    private static final int FRAME_DATA_SIZE = 54;
    private static final int FRAME_LAST_DATA_SIZE = 32;
    private static final int FRAMES_PER_PAGE = 5;
    
    // 传输参数
    private static final int MAX_RETRIES = 3;
    private static final int TIMEOUT_MS = 5000;
    
    // CRC32多项式
    private static final long CRC32_POLYNOMIAL = 0xEDB88320L;
    
    private BluetoothSocket bluetoothSocket;
    private InputStream inputStream;
    private OutputStream outputStream;
    private TransferProgressListener progressListener;
    
    /**
     * 传输进度监听器
     */
    public interface TransferProgressListener {
        void onProgress(int currentPage, int totalPages, String colorType);
        void onError(String error);
        void onComplete();
    }
    
    /**
     * 构造函数
     * @param socket 已连接的蓝牙Socket
     */
    public AndroidImageSender(BluetoothSocket socket) {
        this.bluetoothSocket = socket;
        try {
            this.inputStream = socket.getInputStream();
            this.outputStream = socket.getOutputStream();
        } catch (IOException e) {
            Log.e(TAG, "获取蓝牙流失败", e);
        }
    }
    
    /**
     * 设置进度监听器
     */
    public void setProgressListener(TransferProgressListener listener) {
        this.progressListener = listener;
    }
    
    /**
     * 发送完整图像
     * @param bwImageData 黑白图像数据 (15000字节)
     * @param redImageData 红白图像数据 (15000字节)
     * @param slot 图像槽位 (0-15)
     * @return 传输成功返回true
     */
    public boolean sendImage(byte[] bwImageData, byte[] redImageData, int slot) {
        if (bwImageData.length != IMAGE_SIZE || redImageData.length != IMAGE_SIZE) {
            notifyError("图像数据大小错误，应为" + IMAGE_SIZE + "字节");
            return false;
        }
        
        if (slot < 0 || slot > 15) {
            notifyError("槽位参数错误，应为0-15");
            return false;
        }
        
        try {
            Log.i(TAG, "开始传输图像到槽位" + slot);
            
            // 1. 发送黑白图像
            if (!sendImageColor(bwImageData, COLOR_TYPE_BW, slot)) {
                notifyError("黑白图像传输失败");
                return false;
            }
            
            // 2. 发送红白图像
            if (!sendImageColor(redImageData, COLOR_TYPE_RED, slot)) {
                notifyError("红白图像传输失败");
                return false;
            }
            
            // 3. 发送尾帧
            if (!sendEndFrame(slot)) {
                notifyError("尾帧发送失败");
                return false;
            }
            
            Log.i(TAG, "图像传输完成");
            if (progressListener != null) {
                progressListener.onComplete();
            }
            return true;
            
        } catch (Exception e) {
            Log.e(TAG, "传输过程中发生异常", e);
            notifyError("传输异常: " + e.getMessage());
            return false;
        }
    }
    
    /**
     * 发送单色图像数据
     */
    private boolean sendImageColor(byte[] imageData, byte colorType, int slot) {
        String colorName = (colorType == COLOR_TYPE_BW) ? "黑白" : "红白";
        Log.i(TAG, "开始发送" + colorName + "图像数据");
        
        // 发送首帧
        int retryCount = 0;
        while (retryCount < MAX_RETRIES) {
            if (sendStartFrame(slot, colorType)) {
                break;
            }
            retryCount++;
            Log.w(TAG, colorName + "首帧重试 " + retryCount + "/" + MAX_RETRIES);
        }
        
        if (retryCount >= MAX_RETRIES) {
            Log.e(TAG, colorName + "首帧发送失败");
            return false;
        }
        
        // 发送数据页
        for (int pageSeq = 1; pageSeq <= IMAGE_PAGES_PER_COLOR; pageSeq++) {
            retryCount = 0;
            while (retryCount < MAX_RETRIES) {
                if (sendDataPage(imageData, pageSeq, slot, colorType)) {
                    break;
                }
                retryCount++;
                Log.w(TAG, colorName + "页" + pageSeq + "重试 " + retryCount + "/" + MAX_RETRIES);
            }
            
            if (retryCount >= MAX_RETRIES) {
                Log.e(TAG, colorName + "页" + pageSeq + "发送失败");
                return false;
            }
            
            // 更新进度
            if (progressListener != null) {
                progressListener.onProgress(pageSeq, IMAGE_PAGES_PER_COLOR, colorName);
            }
        }
        
        Log.i(TAG, colorName + "图像数据发送完成");
        return true;
    }
    
    /**
     * 发送首帧
     */
    private boolean sendStartFrame(int slot, byte colorType) {
        try {
            // 构造首帧
            ByteBuffer buffer = ByteBuffer.allocate(8);
            buffer.order(ByteOrder.LITTLE_ENDIAN);
            buffer.putShort((short)PROTOCOL_MAGIC_HOST);
            buffer.put(CMD_IMAGE_TRANSFER);
            buffer.put((byte)(colorType | (slot & 0x0F)));
            buffer.putInt((int)PROTOCOL_END_HOST);
            
            // 发送首帧
            outputStream.write(buffer.array());
            outputStream.flush();
            
            // 等待回复
            return waitStartReply();
            
        } catch (IOException e) {
            Log.e(TAG, "发送首帧失败", e);
            return false;
        }
    }
    
    /**
     * 等待首帧回复
     */
    private boolean waitStartReply() {
        try {
            byte[] reply = new byte[10];
            long startTime = System.currentTimeMillis();
            int bytesRead = 0;
            
            // 读取回复数据
            while (bytesRead < 10 && (System.currentTimeMillis() - startTime) < TIMEOUT_MS) {
                if (inputStream.available() > 0) {
                    int read = inputStream.read(reply, bytesRead, 10 - bytesRead);
                    if (read > 0) {
                        bytesRead += read;
                    }
                }
                Thread.sleep(10);
            }
            
            if (bytesRead != 10) {
                Log.e(TAG, "首帧回复长度错误: " + bytesRead);
                return false;
            }
            
            // 解析回复
            ByteBuffer buffer = ByteBuffer.wrap(reply);
            buffer.order(ByteOrder.LITTLE_ENDIAN);
            
            short magic = buffer.getShort();
            byte command = buffer.get();
            byte slotColor = buffer.get();
            byte status = buffer.get();
            byte reserved = buffer.get();
            int endMagic = buffer.getInt();
            
            if (magic != PROTOCOL_MAGIC_MCU) {
                Log.e(TAG, "首帧回复魔法数错误: 0x" + Integer.toHexString(magic));
                return false;
            }
            
            if (endMagic != (int)PROTOCOL_END_MCU) {
                Log.e(TAG, "首帧回复结束魔法数错误: 0x" + Integer.toHexString(endMagic));
                return false;
            }
            
            switch (status) {
                case MCU_STATUS_OK:
                    Log.d(TAG, "首帧回复: 成功");
                    return true;
                case MCU_STATUS_BUSY:
                    Log.w(TAG, "首帧回复: 忙碌，等待1秒后重试");
                    Thread.sleep(1000);
                    return false;
                default:
                    Log.e(TAG, "首帧回复: 错误 (status=0x" + Integer.toHexString(status) + ")");
                    return false;
            }
            
        } catch (Exception e) {
            Log.e(TAG, "等待首帧回复时出错", e);
            return false;
        }
    }
    
    /**
     * 发送数据页
     */
    private boolean sendDataPage(byte[] imageData, int pageSeq, int slot, byte colorType) {
        // 计算页数据
        int startPos = (pageSeq - 1) * IMAGE_DATA_PER_PAGE;
        int dataSize = (pageSeq == IMAGE_PAGES_PER_COLOR) ? 
                      IMAGE_LAST_PAGE_DATA_SIZE : IMAGE_DATA_PER_PAGE;
        
        byte[] pageData = new byte[IMAGE_DATA_PER_PAGE];
        System.arraycopy(imageData, startPos, pageData, 0, dataSize);
        // 剩余部分填充0
        if (dataSize < IMAGE_DATA_PER_PAGE) {
            Arrays.fill(pageData, dataSize, IMAGE_DATA_PER_PAGE, (byte)0);
        }
        
        // 计算CRC32
        long pageCRC = calculateCRC32(Arrays.copyOf(pageData, dataSize));
        
        try {
            // 发送前4帧 (每帧54字节)
            for (int frameSeq = 1; frameSeq <= 4; frameSeq++) {
                int frameStartPos = (frameSeq - 1) * FRAME_DATA_SIZE;
                byte[] frameData = Arrays.copyOfRange(pageData, frameStartPos, 
                                                    frameStartPos + FRAME_DATA_SIZE);
                sendDataFrame(frameData, pageSeq, frameSeq, slot, colorType);
            }
            
            // 发送第5帧 (32字节数据 + 4字节CRC)
            int frameStartPos = 4 * FRAME_DATA_SIZE;
            byte[] frameData = Arrays.copyOfRange(pageData, frameStartPos, 
                                                frameStartPos + FRAME_LAST_DATA_SIZE);
            
            // 添加CRC32
            ByteBuffer crcBuffer = ByteBuffer.allocate(4);
            crcBuffer.order(ByteOrder.LITTLE_ENDIAN);
            crcBuffer.putInt((int)pageCRC);
            
            byte[] frame5Data = new byte[FRAME_LAST_DATA_SIZE + 4];
            System.arraycopy(frameData, 0, frame5Data, 0, FRAME_LAST_DATA_SIZE);
            System.arraycopy(crcBuffer.array(), 0, frame5Data, FRAME_LAST_DATA_SIZE, 4);
            
            sendDataFrame(frame5Data, pageSeq, 5, slot, colorType);
            
            // 等待页确认
            return waitDataReply(pageSeq);
            
        } catch (Exception e) {
            Log.e(TAG, "发送数据页失败", e);
            return false;
        }
    }
    
    /**
     * 发送数据帧
     */
    private void sendDataFrame(byte[] data, int pageSeq, int frameSeq, int slot, byte colorType) 
            throws IOException {
        ByteBuffer buffer = ByteBuffer.allocate(64);
        buffer.order(ByteOrder.LITTLE_ENDIAN);
        
        // 帧头
        buffer.putShort((short)PROTOCOL_MAGIC_HOST);
        buffer.put(CMD_IMAGE_DATA);
        buffer.put((byte)(colorType | (slot & 0x0F)));
        buffer.put((byte)pageSeq);
        buffer.put((byte)frameSeq);
        
        // 数据
        buffer.put(data);
        
        // 填充到60字节 (64-4字节结束魔法数)
        while (buffer.position() < 60) {
            buffer.put((byte)0);
        }
        
        // 结束魔法数
        buffer.putInt((int)PROTOCOL_END_HOST);
        
        outputStream.write(buffer.array());
        outputStream.flush();
    }
    
    /**
     * 等待数据页回复
     */
    private boolean waitDataReply(int pageSeq) {
        try {
            byte[] reply = new byte[6];
            long startTime = System.currentTimeMillis();
            int bytesRead = 0;
            
            // 读取回复数据
            while (bytesRead < 6 && (System.currentTimeMillis() - startTime) < TIMEOUT_MS) {
                if (inputStream.available() > 0) {
                    int read = inputStream.read(reply, bytesRead, 6 - bytesRead);
                    if (read > 0) {
                        bytesRead += read;
                    }
                }
                Thread.sleep(10);
            }
            
            if (bytesRead != 6) {
                Log.e(TAG, "数据帧回复长度错误: " + bytesRead);
                return false;
            }
            
            // 解析回复
            ByteBuffer buffer = ByteBuffer.wrap(reply);
            buffer.order(ByteOrder.LITTLE_ENDIAN);
            
            short magic = buffer.getShort();
            byte command = buffer.get();
            byte slotColor = buffer.get();
            byte page = buffer.get();
            byte status = buffer.get();
            
            if (magic != PROTOCOL_MAGIC_MCU) {
                Log.e(TAG, "数据帧回复魔法数错误: 0x" + Integer.toHexString(magic));
                return false;
            }
            
            if (page != pageSeq) {
                Log.e(TAG, "数据帧回复页序列错误: 期望" + pageSeq + ", 收到" + page);
                return false;
            }
            
            switch (status) {
                case DATA_STATUS_OK:
                    Log.d(TAG, "页" + pageSeq + "发送成功");
                    return true;
                case DATA_STATUS_CRC_ERROR:
                    Log.w(TAG, "页" + pageSeq + "CRC错误，需要重传");
                    return false;
                case DATA_STATUS_TIMEOUT:
                    Log.w(TAG, "页" + pageSeq + "超时，需要重传");
                    return false;
                default:
                    if ((status & 0xF0) == 0x20) {
                        int missingFrame = status & 0x0F;
                        Log.w(TAG, "页" + pageSeq + "缺失帧" + missingFrame + "，需要重传");
                    } else {
                        Log.e(TAG, "页" + pageSeq + "未知错误: 0x" + Integer.toHexString(status));
                    }
                    return false;
            }
            
        } catch (Exception e) {
            Log.e(TAG, "等待数据帧回复时出错", e);
            return false;
        }
    }
    
    /**
     * 发送尾帧
     */
    private boolean sendEndFrame(int slot) {
        try {
            // 构造尾帧
            ByteBuffer buffer = ByteBuffer.allocate(8);
            buffer.order(ByteOrder.LITTLE_ENDIAN);
            buffer.putShort((short)PROTOCOL_MAGIC_HOST);
            buffer.put(CMD_TRANSFER_END);
            buffer.put((byte)(slot & 0x0F));
            buffer.putInt((int)PROTOCOL_END_HOST);
            
            // 发送尾帧
            outputStream.write(buffer.array());
            outputStream.flush();
            
            // 等待回复
            return waitEndReply();
            
        } catch (IOException e) {
            Log.e(TAG, "发送尾帧失败", e);
            return false;
        }
    }
    
    /**
     * 等待尾帧回复
     */
    private boolean waitEndReply() {
        try {
            byte[] reply = new byte[8];
            long startTime = System.currentTimeMillis();
            int bytesRead = 0;
            
            // 读取回复数据
            while (bytesRead < 8 && (System.currentTimeMillis() - startTime) < TIMEOUT_MS) {
                if (inputStream.available() > 0) {
                    int read = inputStream.read(reply, bytesRead, 8 - bytesRead);
                    if (read > 0) {
                        bytesRead += read;
                    }
                }
                Thread.sleep(10);
            }
            
            if (bytesRead != 8) {
                Log.e(TAG, "尾帧回复长度错误: " + bytesRead);
                return false;
            }
            
            // 解析回复
            ByteBuffer buffer = ByteBuffer.wrap(reply);
            buffer.order(ByteOrder.LITTLE_ENDIAN);
            
            short magic = buffer.getShort();
            byte command = buffer.get();
            byte slotColor = buffer.get();
            int endMagic = buffer.getInt();
            
            if (magic != PROTOCOL_MAGIC_MCU) {
                Log.e(TAG, "尾帧回复魔法数错误: 0x" + Integer.toHexString(magic));
                return false;
            }
            
            if (endMagic != (int)PROTOCOL_END_MCU) {
                Log.e(TAG, "尾帧回复结束魔法数错误: 0x" + Integer.toHexString(endMagic));
                return false;
            }
            
            Log.i(TAG, "尾帧回复: 传输完成");
            return true;
            
        } catch (Exception e) {
            Log.e(TAG, "等待尾帧回复时出错", e);
            return false;
        }
    }
    
    /**
     * 计算CRC32校验值
     */
    private long calculateCRC32(byte[] data) {
        long crc = 0xFFFFFFFFL;
        
        for (byte b : data) {
            crc ^= (b & 0xFF);
            for (int i = 0; i < 8; i++) {
                if ((crc & 1) != 0) {
                    crc = (crc >>> 1) ^ CRC32_POLYNOMIAL;
                } else {
                    crc >>>= 1;
                }
            }
        }
        
        return (~crc) & 0xFFFFFFFFL;
    }
    
    /**
     * 通知错误
     */
    private void notifyError(String error) {
        Log.e(TAG, error);
        if (progressListener != null) {
            progressListener.onError(error);
        }
    }
    
    /**
     * 关闭连接
     */
    public void close() {
        try {
            if (inputStream != null) {
                inputStream.close();
            }
            if (outputStream != null) {
                outputStream.close();
            }
            if (bluetoothSocket != null) {
                bluetoothSocket.close();
            }
        } catch (IOException e) {
            Log.e(TAG, "关闭连接时出错", e);
        }
    }
    
    /**
     * 生成测试图像数据
     * @param colorType 颜色类型 (0=黑白, 1=红白)
     * @return 图像数据字节数组
     */
    public static byte[] generateTestImage(int colorType) {
        byte[] data = new byte[IMAGE_SIZE];
        
        if (colorType == 0) {  // 黑白图像
            // 生成棋盘格图案
            for (int i = 0; i < IMAGE_SIZE; i++) {
                if ((i / 50) % 2 == 0) {
                    data[i] = (byte)0xAA;  // 10101010
                } else {
                    data[i] = (byte)0x55;  // 01010101
                }
            }
        } else {  // 红白图像
            // 生成渐变图案
            for (int i = 0; i < IMAGE_SIZE; i++) {
                data[i] = (byte)((i * 255) / IMAGE_SIZE);
            }
        }
        
        return data;
    }
}

/**
 * 使用示例:
 * 
 * // 1. 连接蓝牙设备
 * BluetoothSocket socket = device.createRfcommSocketToServiceRecord(uuid);
 * socket.connect();
 * 
 * // 2. 创建图像发送器
 * AndroidImageSender sender = new AndroidImageSender(socket);
 * 
 * // 3. 设置进度监听器
 * sender.setProgressListener(new AndroidImageSender.TransferProgressListener() {
 *     @Override
 *     public void onProgress(int currentPage, int totalPages, String colorType) {
 *         int progress = (currentPage * 100) / totalPages;
 *         Log.i("Progress", colorType + "图像进度: " + progress + "% (" + currentPage + "/" + totalPages + ")");
 *     }
 *     
 *     @Override
 *     public void onError(String error) {
 *         Log.e("Error", "传输错误: " + error);
 *     }
 *     
 *     @Override
 *     public void onComplete() {
 *         Log.i("Complete", "图像传输完成!");
 *     }
 * });
 * 
 * // 4. 生成测试图像或加载实际图像
 * byte[] bwImage = AndroidImageSender.generateTestImage(0);  // 黑白图像
 * byte[] redImage = AndroidImageSender.generateTestImage(1); // 红白图像
 * 
 * // 5. 发送图像
 * boolean success = sender.sendImage(bwImage, redImage, 0);  // 发送到槽位0
 * 
 * // 6. 关闭连接
 * sender.close();
 */