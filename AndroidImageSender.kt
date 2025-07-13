/**
 * Android上位机图像传输Kotlin版本
 * 实现与HC32L110下位机的蓝牙通信协议
 * 
 * 使用方法:
 * 1. 确保已获得蓝牙权限
 * 2. 连接到下位机蓝牙设备
 * 3. 调用sendImage()方法传输图像
 */

package com.example.imageTransfer

import android.bluetooth.BluetoothSocket
import android.util.Log
import kotlinx.coroutines.*
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

class AndroidImageSender(private val bluetoothSocket: BluetoothSocket) {
    companion object {
        private const val TAG = "ImageSender"
        
        // 协议常量
        private const val PROTOCOL_MAGIC_HOST = 0xA5A5
        private const val PROTOCOL_MAGIC_MCU = 0x5A5A
        private const val PROTOCOL_END_HOST = 0xA5A5AFAFL
        private const val PROTOCOL_END_MCU = 0x5A5A5F5FL
        
        // 命令类型
        private const val CMD_IMAGE_TRANSFER: Byte = 0xC0.toByte()
        private const val CMD_IMAGE_DATA: Byte = 0xD0.toByte()
        private const val CMD_TRANSFER_END: Byte = 0xC1.toByte()
        
        // 颜色类型
        private const val COLOR_TYPE_BW: Byte = 0x00
        private const val COLOR_TYPE_RED: Byte = 0x10
        
        // 状态码
        private const val MCU_STATUS_OK: Byte = 0x01
        private const val MCU_STATUS_BUSY: Byte = 0x02
        private const val MCU_STATUS_ERROR: Byte = 0xFF.toByte()
        
        private const val DATA_STATUS_OK: Byte = 0x00
        private const val DATA_STATUS_CRC_ERROR: Byte = 0x10
        private const val DATA_STATUS_FRAME_MISSING: Byte = 0x20
        private const val DATA_STATUS_TIMEOUT: Byte = 0x30
        
        // 图像参数
        private const val IMAGE_WIDTH = 400
        private const val IMAGE_HEIGHT = 300
        private const val IMAGE_SIZE = (IMAGE_WIDTH * IMAGE_HEIGHT) / 8  // 15000字节
        private const val IMAGE_PAGES_PER_COLOR = 61
        private const val IMAGE_DATA_PER_PAGE = 248
        private const val IMAGE_LAST_PAGE_DATA_SIZE = 120
        private const val FRAME_DATA_SIZE = 54
        private const val FRAME_LAST_DATA_SIZE = 32
        private const val FRAMES_PER_PAGE = 5
        
        // 传输参数
        private const val MAX_RETRIES = 3
        private const val TIMEOUT_MS = 5000L
        
        // CRC32多项式
        private const val CRC32_POLYNOMIAL = 0xEDB88320L
        
        /**
         * 生成测试图像数据
         * @param colorType 颜色类型 (0=黑白, 1=红白)
         * @return 图像数据字节数组
         */
        fun generateTestImage(colorType: Int): ByteArray {
            val data = ByteArray(IMAGE_SIZE)
            
            if (colorType == 0) {  // 黑白图像
                // 生成棋盘格图案
                for (i in data.indices) {
                    data[i] = if ((i / 50) % 2 == 0) {
                        0xAA.toByte()  // 10101010
                    } else {
                        0x55.toByte()  // 01010101
                    }
                }
            } else {  // 红白图像
                // 生成渐变图案
                for (i in data.indices) {
                    data[i] = ((i * 255) / IMAGE_SIZE).toByte()
                }
            }
            
            return data
        }
    }
    
    /**
     * 传输进度监听器
     */
    interface TransferProgressListener {
        fun onProgress(currentPage: Int, totalPages: Int, colorType: String)
        fun onError(error: String)
        fun onComplete()
    }
    
    /**
     * 传输结果密封类
     */
    sealed class TransferResult {
        object Success : TransferResult()
        data class Error(val message: String) : TransferResult()
    }
    
    private val inputStream: InputStream = bluetoothSocket.inputStream
    private val outputStream: OutputStream = bluetoothSocket.outputStream
    private var progressListener: TransferProgressListener? = null
    
    /**
     * 设置进度监听器
     */
    fun setProgressListener(listener: TransferProgressListener?) {
        this.progressListener = listener
    }
    
    /**
     * 发送完整图像 (协程版本)
     * @param bwImageData 黑白图像数据 (15000字节)
     * @param redImageData 红白图像数据 (15000字节)
     * @param slot 图像槽位 (0-15)
     * @return 传输结果
     */
    suspend fun sendImageAsync(
        bwImageData: ByteArray, 
        redImageData: ByteArray, 
        slot: Int
    ): TransferResult = withContext(Dispatchers.IO) {
        try {
            sendImage(bwImageData, redImageData, slot)
        } catch (e: Exception) {
            TransferResult.Error(e.message ?: "未知错误")
        }
    }
    
    /**
     * 发送完整图像 (同步版本)
     * @param bwImageData 黑白图像数据 (15000字节)
     * @param redImageData 红白图像数据 (15000字节)
     * @param slot 图像槽位 (0-15)
     * @return 传输结果
     */
    @Throws(Exception::class)
    fun sendImage(bwImageData: ByteArray, redImageData: ByteArray, slot: Int): TransferResult {
        // 参数验证
        require(bwImageData.size == IMAGE_SIZE) { 
            "黑白图像数据大小错误，应为${IMAGE_SIZE}字节" 
        }
        require(redImageData.size == IMAGE_SIZE) { 
            "红白图像数据大小错误，应为${IMAGE_SIZE}字节" 
        }
        require(slot in 0..15) { 
            "槽位参数错误，应为0-15" 
        }
        
        return try {
            Log.i(TAG, "开始传输图像到槽位$slot")
            
            // 1. 发送黑白图像
            sendImageColor(bwImageData, COLOR_TYPE_BW, slot)
            
            // 2. 发送红白图像
            sendImageColor(redImageData, COLOR_TYPE_RED, slot)
            
            // 3. 发送尾帧
            sendEndFrame(slot)
            
            Log.i(TAG, "图像传输完成")
            progressListener?.onComplete()
            TransferResult.Success
            
        } catch (e: Exception) {
            Log.e(TAG, "传输过程中发生异常", e)
            val errorMsg = "传输异常: ${e.message}"
            progressListener?.onError(errorMsg)
            TransferResult.Error(errorMsg)
        }
    }
    
    /**
     * 发送单色图像数据
     */
    @Throws(Exception::class)
    private fun sendImageColor(imageData: ByteArray, colorType: Byte, slot: Int) {
        val colorName = if (colorType == COLOR_TYPE_BW) "黑白" else "红白"
        Log.i(TAG, "开始发送${colorName}图像数据")
        
        // 发送首帧
        repeat(MAX_RETRIES) { retryCount ->
            if (sendStartFrame(slot, colorType)) {
                // 发送数据页
                for (pageSeq in 1..IMAGE_PAGES_PER_COLOR) {
                    var pageSuccess = false
                    repeat(MAX_RETRIES) { pageRetryCount ->
                        if (sendDataPage(imageData, pageSeq, slot, colorType)) {
                            pageSuccess = true
                            return@repeat
                        }
                        Log.w(TAG, "${colorName}页${pageSeq}重试 ${pageRetryCount + 1}/$MAX_RETRIES")
                    }
                    
                    if (!pageSuccess) {
                        throw IOException("${colorName}页${pageSeq}发送失败")
                    }
                    
                    // 更新进度
                    progressListener?.onProgress(pageSeq, IMAGE_PAGES_PER_COLOR, colorName)
                }
                
                Log.i(TAG, "${colorName}图像数据发送完成")
                return
            }
            Log.w(TAG, "${colorName}首帧重试 ${retryCount + 1}/$MAX_RETRIES")
        }
        
        throw IOException("${colorName}首帧发送失败")
    }
    
    /**
     * 发送首帧
     */
    @Throws(IOException::class)
    private fun sendStartFrame(slot: Int, colorType: Byte): Boolean {
        // 构造首帧
        val buffer = ByteBuffer.allocate(8).apply {
            order(ByteOrder.LITTLE_ENDIAN)
            putShort(PROTOCOL_MAGIC_HOST.toShort())
            put(CMD_IMAGE_TRANSFER)
            put((colorType.toInt() or (slot and 0x0F)).toByte())
            putInt(PROTOCOL_END_HOST.toInt())
        }
        
        // 发送首帧
        outputStream.write(buffer.array())
        outputStream.flush()
        
        // 等待回复
        return waitStartReply()
    }
    
    /**
     * 等待首帧回复
     */
    @Throws(IOException::class)
    private fun waitStartReply(): Boolean {
        val reply = ByteArray(10)
        val startTime = System.currentTimeMillis()
        var bytesRead = 0
        
        // 读取回复数据
        while (bytesRead < 10 && (System.currentTimeMillis() - startTime) < TIMEOUT_MS) {
            if (inputStream.available() > 0) {
                val read = inputStream.read(reply, bytesRead, 10 - bytesRead)
                if (read > 0) {
                    bytesRead += read
                }
            }
            Thread.sleep(10)
        }
        
        if (bytesRead != 10) {
            Log.e(TAG, "首帧回复长度错误: $bytesRead")
            return false
        }
        
        // 解析回复
        val buffer = ByteBuffer.wrap(reply).apply {
            order(ByteOrder.LITTLE_ENDIAN)
        }
        
        val magic = buffer.short
        val command = buffer.get()
        val slotColor = buffer.get()
        val status = buffer.get()
        val reserved = buffer.get()
        val endMagic = buffer.int
        
        if (magic.toInt() != PROTOCOL_MAGIC_MCU) {
            Log.e(TAG, "首帧回复魔法数错误: 0x${magic.toString(16)}")
            return false
        }
        
        if (endMagic != PROTOCOL_END_MCU.toInt()) {
            Log.e(TAG, "首帧回复结束魔法数错误: 0x${endMagic.toString(16)}")
            return false
        }
        
        return when (status) {
            MCU_STATUS_OK -> {
                Log.d(TAG, "首帧回复: 成功")
                true
            }
            MCU_STATUS_BUSY -> {
                Log.w(TAG, "首帧回复: 忙碌，等待1秒后重试")
                Thread.sleep(1000)
                false
            }
            else -> {
                Log.e(TAG, "首帧回复: 错误 (status=0x${status.toString(16)})")
                false
            }
        }
    }
    
    /**
     * 发送数据页
     */
    @Throws(IOException::class)
    private fun sendDataPage(imageData: ByteArray, pageSeq: Int, slot: Int, colorType: Byte): Boolean {
        // 计算页数据
        val startPos = (pageSeq - 1) * IMAGE_DATA_PER_PAGE
        val dataSize = if (pageSeq == IMAGE_PAGES_PER_COLOR) {
            IMAGE_LAST_PAGE_DATA_SIZE
        } else {
            IMAGE_DATA_PER_PAGE
        }
        
        val pageData = ByteArray(IMAGE_DATA_PER_PAGE).apply {
            System.arraycopy(imageData, startPos, this, 0, dataSize)
            // 剩余部分已经是0，无需额外填充
        }
        
        // 计算CRC32
        val pageCRC = calculateCRC32(pageData.copyOf(dataSize))
        
        // 发送前4帧 (每帧54字节)
        for (frameSeq in 1..4) {
            val frameStartPos = (frameSeq - 1) * FRAME_DATA_SIZE
            val frameData = pageData.copyOfRange(frameStartPos, frameStartPos + FRAME_DATA_SIZE)
            sendDataFrame(frameData, pageSeq, frameSeq, slot, colorType)
        }
        
        // 发送第5帧 (32字节数据 + 4字节CRC)
        val frameStartPos = 4 * FRAME_DATA_SIZE
        val frameData = pageData.copyOfRange(frameStartPos, frameStartPos + FRAME_LAST_DATA_SIZE)
        
        // 添加CRC32
        val crcBuffer = ByteBuffer.allocate(4).apply {
            order(ByteOrder.LITTLE_ENDIAN)
            putInt(pageCRC.toInt())
        }
        
        val frame5Data = frameData + crcBuffer.array()
        sendDataFrame(frame5Data, pageSeq, 5, slot, colorType)
        
        // 等待页确认
        return waitDataReply(pageSeq)
    }
    
    /**
     * 发送数据帧
     */
    @Throws(IOException::class)
    private fun sendDataFrame(
        data: ByteArray, 
        pageSeq: Int, 
        frameSeq: Int, 
        slot: Int, 
        colorType: Byte
    ) {
        val buffer = ByteBuffer.allocate(64).apply {
            order(ByteOrder.LITTLE_ENDIAN)
            
            // 帧头
            putShort(PROTOCOL_MAGIC_HOST.toShort())
            put(CMD_IMAGE_DATA)
            put((colorType.toInt() or (slot and 0x0F)).toByte())
            put(pageSeq.toByte())
            put(frameSeq.toByte())
            
            // 数据
            put(data)
            
            // 填充到60字节 (64-4字节结束魔法数)
            while (position() < 60) {
                put(0.toByte())
            }
            
            // 结束魔法数
            putInt(PROTOCOL_END_HOST.toInt())
        }
        
        outputStream.write(buffer.array())
        outputStream.flush()
    }
    
    /**
     * 等待数据页回复
     */
    @Throws(IOException::class)
    private fun waitDataReply(pageSeq: Int): Boolean {
        val reply = ByteArray(6)
        val startTime = System.currentTimeMillis()
        var bytesRead = 0
        
        // 读取回复数据
        while (bytesRead < 6 && (System.currentTimeMillis() - startTime) < TIMEOUT_MS) {
            if (inputStream.available() > 0) {
                val read = inputStream.read(reply, bytesRead, 6 - bytesRead)
                if (read > 0) {
                    bytesRead += read
                }
            }
            Thread.sleep(10)
        }
        
        if (bytesRead != 6) {
            Log.e(TAG, "数据帧回复长度错误: $bytesRead")
            return false
        }
        
        // 解析回复
        val buffer = ByteBuffer.wrap(reply).apply {
            order(ByteOrder.LITTLE_ENDIAN)
        }
        
        val magic = buffer.short
        val command = buffer.get()
        val slotColor = buffer.get()
        val page = buffer.get()
        val status = buffer.get()
        
        if (magic.toInt() != PROTOCOL_MAGIC_MCU) {
            Log.e(TAG, "数据帧回复魔法数错误: 0x${magic.toString(16)}")
            return false
        }
        
        if (page.toInt() != pageSeq) {
            Log.e(TAG, "数据帧回复页序列错误: 期望$pageSeq, 收到${page.toInt()}")
            return false
        }
        
        return when (status) {
            DATA_STATUS_OK -> {
                Log.d(TAG, "页${pageSeq}发送成功")
                true
            }
            DATA_STATUS_CRC_ERROR -> {
                Log.w(TAG, "页${pageSeq}CRC错误，需要重传")
                false
            }
            DATA_STATUS_TIMEOUT -> {
                Log.w(TAG, "页${pageSeq}超时，需要重传")
                false
            }
            else -> {
                if ((status.toInt() and 0xF0) == 0x20) {
                    val missingFrame = status.toInt() and 0x0F
                    Log.w(TAG, "页${pageSeq}缺失帧${missingFrame}，需要重传")
                } else {
                    Log.e(TAG, "页${pageSeq}未知错误: 0x${status.toString(16)}")
                }
                false
            }
        }
    }
    
    /**
     * 发送尾帧
     */
    @Throws(IOException::class)
    private fun sendEndFrame(slot: Int): Boolean {
        // 构造尾帧
        val buffer = ByteBuffer.allocate(8).apply {
            order(ByteOrder.LITTLE_ENDIAN)
            putShort(PROTOCOL_MAGIC_HOST.toShort())
            put(CMD_TRANSFER_END)
            put((slot and 0x0F).toByte())
            putInt(PROTOCOL_END_HOST.toInt())
        }
        
        // 发送尾帧
        outputStream.write(buffer.array())
        outputStream.flush()
        
        // 等待回复
        return waitEndReply()
    }
    
    /**
     * 等待尾帧回复
     */
    @Throws(IOException::class)
    private fun waitEndReply(): Boolean {
        val reply = ByteArray(8)
        val startTime = System.currentTimeMillis()
        var bytesRead = 0
        
        // 读取回复数据
        while (bytesRead < 8 && (System.currentTimeMillis() - startTime) < TIMEOUT_MS) {
            if (inputStream.available() > 0) {
                val read = inputStream.read(reply, bytesRead, 8 - bytesRead)
                if (read > 0) {
                    bytesRead += read
                }
            }
            Thread.sleep(10)
        }
        
        if (bytesRead != 8) {
            Log.e(TAG, "尾帧回复长度错误: $bytesRead")
            return false
        }
        
        // 解析回复
        val buffer = ByteBuffer.wrap(reply).apply {
            order(ByteOrder.LITTLE_ENDIAN)
        }
        
        val magic = buffer.short
        val command = buffer.get()
        val slotColor = buffer.get()
        val endMagic = buffer.int
        
        if (magic.toInt() != PROTOCOL_MAGIC_MCU) {
            Log.e(TAG, "尾帧回复魔法数错误: 0x${magic.toString(16)}")
            return false
        }
        
        if (endMagic != PROTOCOL_END_MCU.toInt()) {
            Log.e(TAG, "尾帧回复结束魔法数错误: 0x${endMagic.toString(16)}")
            return false
        }
        
        Log.i(TAG, "尾帧回复: 传输完成")
        return true
    }
    
    /**
     * 计算CRC32校验值
     */
    private fun calculateCRC32(data: ByteArray): Long {
        var crc = 0xFFFFFFFFL
        
        for (byte in data) {
            crc = crc xor (byte.toLong() and 0xFF)
            repeat(8) {
                crc = if ((crc and 1) != 0L) {
                    (crc ushr 1) xor CRC32_POLYNOMIAL
                } else {
                    crc ushr 1
                }
            }
        }
        
        return crc.inv() and 0xFFFFFFFFL
    }
    
    /**
     * 关闭连接
     */
    fun close() {
        try {
            inputStream.close()
            outputStream.close()
            bluetoothSocket.close()
        } catch (e: IOException) {
            Log.e(TAG, "关闭连接时出错", e)
        }
    }
}

/**
 * 使用示例:
 * 
 * ```kotlin
 * // 1. 连接蓝牙设备
 * val socket = device.createRfcommSocketToServiceRecord(uuid)
 * socket.connect()
 * 
 * // 2. 创建图像发送器
 * val sender = AndroidImageSender(socket)
 * 
 * // 3. 设置进度监听器
 * sender.setProgressListener(object : AndroidImageSender.TransferProgressListener {
 *     override fun onProgress(currentPage: Int, totalPages: Int, colorType: String) {
 *         val progress = (currentPage * 100) / totalPages
 *         Log.i("Progress", "${colorType}图像进度: $progress% ($currentPage/$totalPages)")
 *     }
 *     
 *     override fun onError(error: String) {
 *         Log.e("Error", "传输错误: $error")
 *     }
 *     
 *     override fun onComplete() {
 *         Log.i("Complete", "图像传输完成!")
 *     }
 * })
 * 
 * // 4. 生成测试图像或加载实际图像
 * val bwImage = AndroidImageSender.generateTestImage(0)  // 黑白图像
 * val redImage = AndroidImageSender.generateTestImage(1) // 红白图像
 * 
 * // 5. 发送图像 (协程版本)
 * lifecycleScope.launch {
 *     when (val result = sender.sendImageAsync(bwImage, redImage, 0)) {
 *         is AndroidImageSender.TransferResult.Success -> {
 *             Log.i("Success", "图像传输成功!")
 *         }
 *         is AndroidImageSender.TransferResult.Error -> {
 *             Log.e("Error", "传输失败: ${result.message}")
 *         }
 *     }
 * }
 * 
 * // 或者使用同步版本
 * try {
 *     val result = sender.sendImage(bwImage, redImage, 0)
 *     when (result) {
 *         is AndroidImageSender.TransferResult.Success -> {
 *             Log.i("Success", "图像传输成功!")
 *         }
 *         is AndroidImageSender.TransferResult.Error -> {
 *             Log.e("Error", "传输失败: ${result.message}")
 *         }
 *     }
 * } catch (e: Exception) {
 *     Log.e("Exception", "传输异常", e)
 * }
 * 
 * // 6. 关闭连接
 * sender.close()
 * ```
 */