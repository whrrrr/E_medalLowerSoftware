# 图像显示模块内存优化总结 - 流式处理版本

## 优化目标
由于芯片内存资源有限，原有的图像显示模块使用了两个大型全局缓冲区：
- `g_bw_image_data[IMAGE_SIZE]` (约15KB) - 黑白图像数据缓冲区
- `g_red_image_data[IMAGE_SIZE]` (约15KB) - 红白图像数据缓冲区

总计约30KB的内存占用对于资源受限的嵌入式系统来说过于庞大，需要进行优化。

## 优化方案
采用**流式处理**方案，从Flash中逐页读取图像数据并直接发送给EPD，避免在内存中缓存完整图像。

### 核心思路
1. **移除大型全局缓冲区**：删除30KB的图像数据缓冲区
2. **逐页处理**：使用256字节的小缓冲区逐页读取Flash数据
3. **直接传输**：读取后立即发送到EPD，无需中间存储
4. **参考EPD接口**：基于`EPD_WhiteScreenGDEY042Z98ALLBlack`函数的实现模式

## 具体实现

### 1. 移除大型缓冲区
```c
// 原代码（已删除）
// static uint8_t g_bw_image_data[IMAGE_SIZE];   // 黑白图像数据缓冲区
// static uint8_t g_red_image_data[IMAGE_SIZE];  // 红白图像数据缓冲区

// 新代码
// 移除大型全局缓冲区，改为逐页处理以节省内存
```

### 2. 新增流式处理函数

#### `send_bw_image_to_epd()` - 发送黑白图像
- 发送EPD写入指令 `0x24`
- 逐页从Flash读取黑白图像数据
- 直接通过SPI发送到EPD
- 跳过8字节页头，只发送图像数据

#### `send_red_image_to_epd()` - 发送红白图像
- 发送EPD写入指令 `0x26`
- 逐页从Flash读取红白图像数据
- 直接通过SPI发送到EPD
- 跳过8字节页头，只发送图像数据

#### `display_image_from_flash_streaming()` - 流式显示控制
- 依次调用黑白和红白图像发送函数
- 调用`EPD_UpdateGDEY042Z98ALL_fast()`更新显示

### 3. EPD通信协议实现
参考原有的EPD函数，实现了完整的SPI通信流程：

```c
// 黑白图像写入流程
spiWriteCmd(0x24);        // 写指令0x24，表示开始写黑白内存
DC_H;                     // 拉高数据指令线
Spi_SetCS(TRUE);
Spi_SetCS(FALSE);         // 拉低片选
// ... 逐字节发送图像数据
Spi_SetCS(TRUE);          // 拉高片选
DC_L;                     // 拉低数据指令线
delay1ms(2);

// 红白图像写入流程
spiWriteCmd(0x26);        // 写指令0x26，表示开始写红白内存
// ... 类似的SPI通信流程

// 最后更新显示
EPD_UpdateGDEY042Z98ALL_fast();
```

### 4. 修改显示流程

#### 原流程
```
LOADING -> DECOMPRESSING -> DISPLAYING -> COMPLETE
           ↓                ↓
    加载完整图像到内存    从内存显示到EPD
```

#### 新流程
```
LOADING -> DISPLAYING -> COMPLETE
           ↓
    直接从Flash流式显示到EPD
```

跳过了`DECOMPRESSING`状态，因为不再需要预先加载所有数据到内存。

## 优化效果

### 内存节省
- **删除**: 30KB大型图像缓冲区
- **保留**: 256字节页缓冲区（复用`g_general_buffer_256`）
- **净节省**: 约30KB内存空间
- **节省比例**: 99.2%的内存占用减少

### 性能影响
- **优势**: 大幅减少内存占用，避免内存不足问题
- **劣势**: 增加Flash读取次数（每页一次），可能略微增加显示时间
- **权衡**: 对于内存受限的系统，这是必要的优化

## 代码变更总结

### 修改的文件
1. **image_display_new.c**
   - 移除大型全局缓冲区定义
   - 新增流式处理函数
   - 修改显示流程逻辑
   - 添加EPD通信协议实现

2. **image_display_new.h**
   - 新增流式显示函数声明
   - 保留兼容性函数声明（已弃用）

### 新增的函数
- `send_bw_image_to_epd()` - 发送黑白图像到EPD
- `send_red_image_to_epd()` - 发送红白图像到EPD
- `display_image_from_flash_streaming()` - 流式显示控制
- `display_image_to_epd_streaming()` - 流式显示接口

### 修改的函数
- `image_display_new_process()` - 简化状态机，使用流式显示
- `display_image_to_epd()` - 标记为已弃用，提供兼容性

## 使用注意事项

1. **函数调用**: 使用新的`display_image_to_epd_streaming()`函数替代原有的`display_image_to_epd()`

2. **错误处理**: 流式处理中如果某页读取失败，会立即停止并恢复SPI状态

3. **进度监控**: 每10页显示一次进度信息，便于调试和监控

4. **兼容性**: 保留了原有函数接口，但标记为已弃用

## 验证方法

1. **内存使用验证**
   ```c
   // 编译后检查.map文件中的内存使用情况
   // 确认不再有大型图像缓冲区分配
   ```

2. **功能验证**
   ```c
   // 调用新的显示接口
   result = image_display_new_show(&display, slot);
   while (display.state != DISPLAY_STATE_COMPLETE && 
          display.state != DISPLAY_STATE_ERROR) {
       image_display_new_process(&display);
   }
   ```

3. **性能测试**
   - 测试显示时间是否在可接受范围内
   - 验证Flash读取操作的稳定性
   - 确认EPD显示效果正常

## 总结

本次优化成功实现了从**内存密集型**到**流式处理**的转换，在保持功能完整性的同时，将内存占用从30KB降低到256字节，为资源受限的嵌入式系统提供了可行的解决方案。

这种优化方案特别适用于：
- 内存资源紧张的嵌入式系统
- 需要显示大尺寸图像的应用
- Flash存储充足但RAM有限的场景

通过合理的架构设计和流式处理技术，成功解决了内存瓶颈问题，为系统的稳定运行提供了保障。