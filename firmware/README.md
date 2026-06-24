# 固件目录

此目录用于存放 ESP32 固件文件，支持 OTA 远程升级。

## 使用说明

### 1. 编译固件

```bash
pio run
```

### 2. 复制固件到此处

```bash
cp .pio/build/4d_systems_esp32s3_gen4_r8n16/firmware.bin firmware/
```

### 3. OTA 升级 URL

固件上传到 GitHub Pages 后，URL 格式为：

```
https://sukiminami.github.io/SLEEP-Monitoring/firmware/firmware.bin
```

### 4. 发送 OTA 命令

```json
{
    "cmdID": 3,
    "versionOld": "1.0.0",
    "versionNew": "1.0.1",
    "hdVersion": "V1.0",
    "otaUrl": "https://sukiminami.github.io/SLEEP-Monitoring/firmware/firmware.bin"
}
```

## 固件版本管理

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0.0 | 2024-01-01 | 初始版本 |

## 注意事项

1. 固件文件较大（约 1MB），建议使用 GitHub Releases 管理正式版本
2. 开发测试阶段可以直接提交到 firmware 目录
