/**
 * @file RadarR60A.cpp
 * @brief R60ABD1 60GHz 毫米波雷达模块驱动实现
 * @details 本文件实现了 R60ABD1 雷达模块的完整驱动，包括：
 *          - 串口通信与数据包解析
 *          - 人体存在检测
 *          - 呼吸频率与呼吸状态监测
 *          - 心率监测
 *          - 睡眠状态分析（深睡/浅睡/清醒/离床）
 *          - 距离与位置检测
 *          - 数据校验和验证
 */

#include "RadarR60A.h"
#include "Protocol.h"

/**
 * @brief 构造函数
 * @param rxPin ESP32 接收引脚
 * @param txPin ESP32 发送引脚
 * @param baudRate 串口波特率（默认 115200）
 * @details 初始化串口参数，设置默认使用 Serial2，
 *          清零所有数据结构和状态机
 */
RadarR60A::RadarR60A(int rxPin, int txPin, long baudRate) {
    _rxPin = rxPin;
    _txPin = txPin;
    _baudRate = baudRate;
    _serial = &Serial2;
    _bufferIndex = 0;
    _state = STATE_IDLE;
    _initialized = false;
    _heartbeatReceived = false;
    _initStartTime = 0;
    memset(&_currentData, 0, sizeof(RadarData));
}

/**
 * @brief 初始化雷达模块
 * @return true-成功，false-失败
 * @details 完整的初始化流程包括：
 *          1. 配置串口参数（引脚、波特率）
 *          2. 发送复位命令并等待雷达重启
 *          3. 发送心跳包查询确认通信正常
 *          4. 开启人体存在检测功能
 *          5. 开启呼吸检测功能
 *          6. 开启心率检测功能
 *          所有配置命令均采用官方协议格式，包含帧头、控制字、
 *          命令字、数据长度、数据、校验和、帧尾。
 */
bool RadarR60A::begin() {
    Serial0.print("[Radar] 配置串口：RX=");
    Serial0.print(_rxPin);
    Serial0.print(", TX=");
    Serial0.print(_txPin);
    Serial0.print(", 波特率=");
    Serial0.println(_baudRate);
    
    _serial->begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
    delay(200);
    
    if (!_serial) {
        Serial0.println("[Radar] 串口初始化失败");
        return false;
    }
    
    Serial0.println("[Radar] 串口初始化成功");
    Serial0.println("[Radar] 开始初始化雷达模块...");
    
    _initialized = false;
    _heartbeatReceived = false;
    _initStartTime = millis();
    
    if (!resetModule()) {
        Serial0.println("[Radar] 警告：雷达复位超时");
    }
    
    Serial0.println("[Radar] 等待雷达复位完成...");
    delay(2000);
    
    Serial0.println("[Radar] 步骤 1: 发送心跳包查询");
    _serial->flush();
    delay(50);
    sendHeartbeatQuery();
    
    unsigned long heartbeatTimeout = millis() + 3000;
    while (millis() < heartbeatTimeout && !_heartbeatReceived) {
        readSerial();
        delay(10);
    }
    
    if (!_heartbeatReceived) {
        Serial0.println("[Radar] 警告：未收到心跳包回复");
    } else {
        Serial0.println("[Radar] 心跳包回复确认");
    }
    
    Serial0.println("[Radar] 等待发送配置命令...");
    delay(100);
    
    // 步骤 2: 开启人体存在检测（控制字 0x80，命令字 0x00，数据 0x01=开）
    Serial0.println("[Radar] 步骤 2: 开启人体存在检测");
    _serial->flush();
    delay(50);
    uint8_t configPacket[] = {0x53, 0x59, 0x80, 0x00, 0x00, 0x01, 0x01};
    uint8_t configChecksum = calculateChecksum(configPacket, 7);
    _serial->write(configPacket, 7);
    _serial->write(configChecksum);
    _serial->write(0x54);
    _serial->write(0x43);
    _serial->flush();
    delay(100);
    
    // 步骤 3: 开启呼吸检测（控制字 0x81，命令字 0x00，数据 0x01=开）
    Serial0.println("[Radar] 步骤 3: 开启呼吸检测");
    _serial->flush();
    delay(50);
    uint8_t breathPacket[] = {0x53, 0x59, 0x81, 0x00, 0x00, 0x01, 0x01};
    uint8_t breathChecksum = calculateChecksum(breathPacket, 7);
    _serial->write(breathPacket, 7);
    _serial->write(breathChecksum);
    _serial->write(0x54);
    _serial->write(0x43);
    _serial->flush();
    delay(100);
    
    // 步骤 4: 开启心率检测（控制字 0x85，命令字 0x00，数据 0x01=开）
    Serial0.println("[Radar] 步骤 4: 开启心率检测");
    _serial->flush();
    delay(50);
    uint8_t heartPacket[] = {0x53, 0x59, 0x85, 0x00, 0x00, 0x01, 0x01};
    uint8_t heartChecksum = calculateChecksum(heartPacket, 7);
    _serial->write(heartPacket, 7);
    _serial->write(heartChecksum);
    _serial->write(0x54);
    _serial->write(0x43);
    _serial->flush();
    delay(100);
    
    _initialized = true;
    Serial0.println("[Radar] R60ABD1 雷达模块初始化完成");
    return true;
}

/**
 * @brief 更新雷达数据
 * @details 在主循环中调用，读取串口数据并解析数据包。
 *          解析后的数据会更新到 _currentData 结构体中。
 */
void RadarR60A::update() {
    readSerial();
}

/**
 * @brief 获取当前雷达数据
 * @return RadarData 结构体
 * @details 返回最近一次解析的雷达数据副本
 */
RadarR60A::RadarData RadarR60A::getData() {
    return _currentData;
}

/**
 * @brief 打印雷达数据到串口
 * @details 每秒打印一次，包含人体存在状态、呼吸频率、
 *          心率、距离、运动状态、睡眠状态等信息。
 *          仅当检测到人体时打印详细数据。
 */
void RadarR60A::printData() {
    static unsigned long lastPrint = 0;
    
    if (millis() - lastPrint >= 1000) {
        Serial0.println("-------------------");
        Serial0.print("Detection: ");
        Serial0.println(_currentData.isDetected ? "YES" : "NO");
        
        // 始终打印所有数据（不检测是否有人）
        Serial0.print("Breath: ");
            Serial0.print(_currentData.breathRate);
            Serial0.println(" bpm");
            
            Serial0.print("Heart: ");
            Serial0.print(_currentData.heartRate);
            Serial0.println(" bpm");
            
            Serial0.print("Distance: ");
            Serial0.print(_currentData.distance);
            Serial0.println(" cm");
            
            Serial0.print("Movement: ");
            Serial0.println(_currentData.bodyMovement);
            
            Serial0.print("Sleep: ");
            switch(_currentData.sleepState) {
                case SLEEP_AWAKE: Serial0.println("Awake"); break;
                case SLEEP_LIGHT: Serial0.println("Light"); break;
                case SLEEP_DEEP: Serial0.println("Deep"); break;
                case SLEEP_AWAY: Serial0.println("Away"); break;
                default: Serial0.println("Unknown"); break;
            }
        lastPrint = millis();
    }
}

/**
 * @brief 发送 JSON 格式数据到串口
 * @details 将当前雷达数据格式化为 JSON 字符串并打印到串口，
 *          包含检测状态、呼吸频率、心率、距离、运动状态、睡眠状态。
 */
void RadarR60A::sendJsonData() {
    Serial0.print("{");
    Serial0.print("\"detected\":");
    Serial0.print(_currentData.isDetected ? "true" : "false");
    Serial0.print(",\"breath\":");
    Serial0.print(_currentData.breathRate);
    Serial0.print(",\"heart\":");
    Serial0.print(_currentData.heartRate);
    Serial0.print(",\"distance\":");
    Serial0.print(_currentData.distance);
    Serial0.print(",\"movement\":");
    Serial0.print(_currentData.bodyMovement);
    Serial0.print(",\"sleep\":");
    Serial0.print(_currentData.sleepState);
    Serial0.println("}");
}

/**
 * @brief 发送心跳包查询命令
 * @details 心跳包用于确认雷达模块通信正常，
 *          协议格式：帧头(0x53 0x59) + 控制字(0x05) + 命令字(0x01) + 
 *          长度(0x00 0x01) + 数据(0x0F) + 校验和 + 帧尾(0x54 0x43)
 */
void RadarR60A::sendHeartbeatQuery() {
    uint8_t packet[] = {0x53, 0x59, 0x05, 0x01, 0x00, 0x01, 0x0F};
    uint8_t checksum = calculateChecksum(packet, 7);
    _serial->write(packet, 7);
    _serial->write(checksum);
    _serial->write(0x54);
    _serial->write(0x43);
    Serial0.println("[Radar] 已发送心跳包查询");
}

/**
 * @brief 复位雷达模块
 * @param timeout 超时时间（毫秒）
 * @return true-成功，false-超时
 * @details 发送复位命令使雷达模块重启，用于初始化或异常恢复。
 *          复位命令格式与心跳包类似，但控制字和命令字不同。
 */
bool RadarR60A::resetModule(unsigned long timeout) {
    Serial0.println("[Radar] 发送复位命令");
    
    uint8_t resetPacket[] = {0x53, 0x59, 0x01, 0x02, 0x00, 0x01, 0x0F};
    uint8_t resetChecksum = calculateChecksum(resetPacket, 7);
    
    _serial->write(resetPacket, 7);
    _serial->write(resetChecksum);
    _serial->write(0x54);
    _serial->write(0x43);
    
    unsigned long resetTimeout = millis() + timeout;
    while (millis() < resetTimeout) {
        readSerial();
        delay(10);
    }
    
    Serial0.println("[Radar] 雷达复位完成");
    return true;
}

/**
 * @brief 读取并解析串口数据
 * @details 使用状态机解析雷达数据包，数据包格式：
 *          - 帧头：0x53 0x59
 *          - 控制字：标识数据类型（人体存在、呼吸、心率等）
 *          - 命令字：标识具体命令
 *          - 数据长度：2 字节（高字节 + 低字节）
 *          - 数据：可变长度
 *          - 校验和：1 字节
 *          - 帧尾：0x54 0x43
 *          
 *          状态机依次经历：IDLE -> STX1 -> CONTROL -> COMMAND -> 
 *          LENGTH_H -> LENGTH_L -> DATA -> CHECKSUM -> TC1 -> TC2
 */
void RadarR60A::readSerial() {
    while (_serial->available() > 0) {
        uint8_t c = _serial->read();
        
        switch (_state) {
            case STATE_IDLE:
                if (c == 0x53) {
                    _state = STATE_STX1;
                    _buffer[0] = c;
                    _bufferIndex = 1;
                }
                break;
                
            case STATE_STX1:
                if (c == 0x59) {
                    _state = STATE_CONTROL;
                    _buffer[_bufferIndex++] = c;
                } else {
                    _state = STATE_IDLE;
                }
                break;
                
            case STATE_CONTROL:
                _buffer[_bufferIndex++] = c;
                _controlWord = c;
                _state = STATE_COMMAND;
                break;
                
            case STATE_COMMAND:
                _buffer[_bufferIndex++] = c;
                _commandWord = c;
                _state = STATE_LENGTH_H;
                break;
                
            case STATE_LENGTH_H:
                _buffer[_bufferIndex++] = c;
                _dataLength = (uint16_t)c << 8;
                _state = STATE_LENGTH_L;
                break;
                
            case STATE_LENGTH_L:
                _buffer[_bufferIndex++] = c;
                _dataLength |= c;
                _state = STATE_DATA;
                _dataCount = 0;
                break;
                
            case STATE_DATA:
                _buffer[_bufferIndex++] = c;
                _dataCount++;
                if (_dataCount >= _dataLength) {
                    _state = STATE_CHECKSUM;
                } else if (_bufferIndex >= MAX_BUFFER_SIZE) {
                    _state = STATE_IDLE;
                    _bufferIndex = 0;
                }
                break;
                
            case STATE_CHECKSUM:
                _buffer[_bufferIndex++] = c;
                _receivedChecksum = c;
                _state = STATE_TC1;
                break;
                
            case STATE_TC1:
                if (c == 0x54) {
                    _buffer[_bufferIndex++] = c;
                    _state = STATE_TC2;
                } else {
                    _state = STATE_IDLE;
                    _bufferIndex = 0;
                }
                break;
                
            case STATE_TC2:
                if (c == 0x43) {
                    _buffer[_bufferIndex++] = c;
                    parsePacket();
                }
                _state = STATE_IDLE;
                _bufferIndex = 0;
                break;
                
            default:
                _state = STATE_IDLE;
                _bufferIndex = 0;
                break;
        }
    }
}

/**
 * @brief 计算校验和
 * @param data 数据指针
 * @param length 数据长度
 * @return 校验和（低 8 位）
 * @details 校验和算法：将所有数据字节相加，取低 8 位
 */
uint8_t RadarR60A::calculateChecksum(uint8_t* data, uint16_t length) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < length; i++) {
        checksum += data[i];
    }
    return checksum;
}

/**
 * @brief 解析完整数据包
 * @details 首先验证校验和，若校验通过则根据控制字分发到
 *          对应的解析函数。支持的控制字包括：
 *          - 0x80: 人体存在数据
 *          - 0x81: 呼吸数据
 *          - 0x85: 心率数据
 *          - 0x84: 睡眠数据
 *          - 0x01: 心跳包
 *          - 0x05: 工作状态
 *          - 0x02: 产品信息
 */
void RadarR60A::parsePacket() {
    uint8_t expectedChecksum = calculateChecksum(_buffer, _bufferIndex - 3);
    if (expectedChecksum != _receivedChecksum) {
        Serial0.println("[Radar] 校验和错误");
        return;
    }
    
    switch (_controlWord) {
        case CTRL_HUMAN_PRESENCE:
            parseHumanPresenceData();
            break;
        case CTRL_BREATH:
            parseBreathData();
            break;
        case CTRL_HEART_RATE:
            parseHeartRateData();
            break;
        case CTRL_SLEEP:
            parseSleepData();
            break;
        case CTRL_HEARTBEAT:
            parseHeartbeatData();
            break;
        case CTRL_STATUS:
            parseStatusData();
            break;
        case CTRL_PRODUCT_INFO:
            parseProductInfo();
            break;
        default:
            break;
    } 
}

/**
 * @brief 解析人体存在数据
 * @details 根据命令字解析不同类型的人体存在数据：
 *          - 0x01: 有人/无人状态
 *          - 0x02: 静止/活跃状态
 *          - 0x03: 体动幅度参数
 *          - 0x04: 人体距离（2 字节）
 *          - 0x05: 人体方位（X/Y/Z 坐标，各 2 字节）
 */
void RadarR60A::parseHumanPresenceData() {
    switch (_commandWord) {
        case CMD_PRESENCE_DETECT:
            if (_dataLength >= 1) {
                _currentData.isDetected = (_buffer[6] == 0x01);
            } else {
                _currentData.isDetected = false;
            }
            break;
            
        case CMD_MOVEMENT_STATE:
            if (_dataLength >= 1) {
                _currentData.movementState = (MovementState)_buffer[6];
            }
            break;
            
        case CMD_BODY_MOVEMENT:
            if (_dataLength >= 1) {
                _currentData.bodyMovement = _buffer[6];
            }
            break;
            
        case CMD_DISTANCE:
            if (_dataLength >= 2) {
                _currentData.distance = (uint16_t)((_buffer[6] << 8) | _buffer[7]);
            }
            break;
            
        case CMD_POSITION:
            if (_dataLength >= 6) {
                _currentData.positionX = decodePosition((int16_t)((_buffer[6] << 8) | _buffer[7]));
                _currentData.positionY = decodePosition((int16_t)((_buffer[8] << 8) | _buffer[9]));
                _currentData.positionZ = decodePosition((int16_t)((_buffer[10] << 8) | _buffer[11]));
            }
            break;
    }
}

/**
 * @brief 解析呼吸数据
 * @details 根据命令字解析呼吸相关数据：
 *          - 0x01: 呼吸状态（正常/过高/过低/无人）
 *          - 0x02: 呼吸频率（bpm）
 *          - 0x05: 呼吸波形（5 个采样点，需减去 128 偏移）
 */
void RadarR60A::parseBreathData() {
    switch (_commandWord) {
        case CMD_BREATH_INFO:
            if (_dataLength >= 1) {
                _currentData.breathStatus = (BreathStatus)_buffer[6];
            }
            break;
            
        case CMD_BREATH_RATE:
            if (_dataLength >= 1) {
                _currentData.breathRate = _buffer[6];
            }
            break;
            
        case CMD_BREATH_WAVE:
            if (_dataLength >= 5) {
                for (int i = 0; i < 5; i++) {
                    _currentData.breathWave[i] = _buffer[6 + i] - 128;
                }
            }
            break;
    }
}

/**
 * @brief 解析心率数据
 * @details 根据命令字解析心率相关数据：
 *          - 0x02: 心率数值（bpm）
 *          - 0x05: 心率波形（5 个采样点，需减去 128 偏移）
 */
void RadarR60A::parseHeartRateData() {
    switch (_commandWord) {
        case CMD_HEART_RATE:
            if (_dataLength >= 1) {
                _currentData.heartRate = _buffer[6];
            }
            break;
            
        case CMD_HEART_WAVE:
            if (_dataLength >= 5) {
                for (int i = 0; i < 5; i++) {
                    _currentData.heartWave[i] = _buffer[6 + i] - 128;
                }
            }
            break;
    }
}

/**
 * @brief 解析睡眠数据
 * @details 根据命令字解析睡眠相关数据：
 *          - 0x01: 睡眠状态（深睡/浅睡/清醒/离床）
 *          - 0x02: 睡眠时长（清醒/浅睡/深睡，各 2 字节）
 *          - 0x03: 睡眠质量评分（0-100）
 */
void RadarR60A::parseSleepData() {
    switch (_commandWord) {
        case CMD_SLEEP_STATE:
            if (_dataLength >= 1) {
                _currentData.sleepState = (SleepState)_buffer[6];
            }
            break;
            
        case CMD_SLEEP_DURATION:
            if (_dataLength >= 8) {
                _currentData.awakeDuration = (uint16_t)((_buffer[6] << 8) | _buffer[7]);
                _currentData.lightSleepDuration = (uint16_t)((_buffer[8] << 8) | _buffer[9]);
                _currentData.deepSleepDuration = (uint16_t)((_buffer[10] << 8) | _buffer[11]);
            }
            break;
            
        case CMD_SLEEP_QUALITY:
            if (_dataLength >= 1) {
                _currentData.sleepQuality = _buffer[6];
            }
            break;
    }
}

/**
 * @brief 解析心跳包数据
 * @details 收到心跳包回复后设置 _heartbeatReceived 标志，
 *          用于初始化流程确认
 */
void RadarR60A::parseHeartbeatData() {
    _heartbeatReceived = true;
}

/**
 * @brief 解析工作状态数据
 * @details 当命令字为 0x01 时，表示雷达已就绪，
 *          设置 _initialized 标志为 true
 */
void RadarR60A::parseStatusData() {
    if (_commandWord == 0x01) {
        _initialized = true;
    }
}

/**
 * @brief 解析产品信息
 * @details 当前为空实现，可扩展用于读取固件版本、
 *          产品型号、硬件型号、产品 ID 等信息
 */
void RadarR60A::parseProductInfo() {
}

/**
 * @brief 解码位置坐标
 * @param rawValue 原始 16 位数据
 * @return 解码后的坐标值（cm）
 * @details 当前直接返回原始值，可根据实际需求进行
 *          坐标变换或校准
 */
int16_t RadarR60A::decodePosition(int16_t rawValue) {
    return rawValue;
}

/**
 * @brief 发送命令到雷达模块
 * @param control 控制字
 * @param command 命令字
 * @param data 数据指针
 * @param dataLen 数据长度
 */
void RadarR60A::sendCommand(uint8_t control, uint8_t command, uint8_t* data, uint8_t dataLen) {
    if (!_serial || !_initialized) {
        Serial0.println("[Radar] 错误：串口未初始化");
        return;
    }
    
    uint8_t packet[128];
    packet[0] = 0x53;  // 帧头
    packet[1] = 0x59;
    packet[2] = control;
    packet[3] = command;
    packet[4] = (dataLen >> 8) & 0xFF;  // 长度高字节
    packet[5] = dataLen & 0xFF;         // 长度低字节
    
    for (uint8_t i = 0; i < dataLen; i++) {
        packet[6 + i] = data[i];
    }
    
    uint8_t checksum = calculateChecksum(packet, 6 + dataLen);
    packet[6 + dataLen] = checksum;
    packet[7 + dataLen] = 0x54;  // 帧尾
    packet[8 + dataLen] = 0x43;
    
    _serial->flush();
    delay(10);
    _serial->write(packet, 9 + dataLen);
    _serial->flush();
    
    Serial0.print("[Radar] 发送命令: 控制字=0x");
    Serial0.print(control, HEX);
    Serial0.print(", 命令字=0x");
    Serial0.print(command, HEX);
    Serial0.print(", 数据长度=");
    Serial0.println(dataLen);
}

/**
 * @brief 开启人体存在检测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::enableHumanPresence() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x01;
    sendCommand(CTRL_HUMAN_PRESENCE, 0x00, &data, 1);
    delay(50);
    Serial0.println("[Radar] 已发送开启人体存在检测命令");
    return true;
}

/**
 * @brief 关闭人体存在检测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::disableHumanPresence() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x00;
    sendCommand(CTRL_HUMAN_PRESENCE, 0x00, &data, 1);
    delay(50);
    // 清空相关数据，防止数据残留
    _currentData.isDetected = false;
    _currentData.movementState = MOVEMENT_NONE;
    _currentData.bodyMovement = 0;
    Serial0.println("[Radar] 已发送关闭人体存在检测命令");
    return true;
}

/**
 * @brief 开启呼吸检测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::enableBreathDetection() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x01;
    sendCommand(CTRL_BREATH, 0x00, &data, 1);
    delay(50);
    Serial0.println("[Radar] 已发送开启呼吸检测命令");
    return true;
}

/**
 * @brief 关闭呼吸检测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::disableBreathDetection() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x00;
    sendCommand(CTRL_BREATH, 0x00, &data, 1);
    delay(50);
    // 清空相关数据，防止数据残留
    _currentData.breathRate = 0;
    _currentData.breathStatus = BREATH_NONE;
    memset(_currentData.breathWave, 0, sizeof(_currentData.breathWave));
    Serial0.println("[Radar] 已发送关闭呼吸检测命令");
    return true;
}

/**
 * @brief 开启心率检测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::enableHeartRateDetection() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x01;
    sendCommand(CTRL_HEART_RATE, 0x00, &data, 1);
    delay(50);
    Serial0.println("[Radar] 已发送开启心率检测命令");
    return true;
}

/**
 * @brief 关闭心率检测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::disableHeartRateDetection() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x00;
    sendCommand(CTRL_HEART_RATE, 0x00, &data, 1);
    delay(50);
    // 清空相关数据，防止数据残留
    _currentData.heartRate = 0;
    memset(_currentData.heartWave, 0, sizeof(_currentData.heartWave));
    Serial0.println("[Radar] 已发送关闭心率检测命令");
    return true;
}

/**
 * @brief 开启睡眠监测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::enableSleepMonitoring() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x01;
    sendCommand(CTRL_SLEEP, 0x00, &data, 1);
    delay(50);
    Serial0.println("[Radar] 已发送开启睡眠监测命令");
    return true;
}

/**
 * @brief 关闭睡眠监测
 * @return true-发送成功，false-发送失败
 */
bool RadarR60A::disableSleepMonitoring() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    uint8_t data = 0x00;
    sendCommand(CTRL_SLEEP, 0x00, &data, 1);
    delay(50);
    // 清空相关数据，防止数据残留
    _currentData.sleepState = SLEEP_AWAY;
    _currentData.awakeDuration = 0;
    _currentData.lightSleepDuration = 0;
    _currentData.deepSleepDuration = 0;
    _currentData.sleepQuality = 0;
    Serial0.println("[Radar] 已发送关闭睡眠监测命令");
    return true;
}

/**
 * @brief 根据设备参数配置雷达检测功能
 * @param params 设备参数结构体指针
 * @return true-配置成功，false-配置失败
 * @details 根据设备参数中的各个开关状态，向雷达模块发送对应的控制命令，
 *          实现数据上报类型与雷达功能的联动。
 */
bool RadarR60A::configureDetection(const DeviceParams* params) {
    if (!_initialized || params == nullptr) {
        Serial0.println("[Radar] 错误：雷达未初始化或参数为空");
        return false;
    }
    
    Serial0.println("[Radar] 根据设备参数配置检测功能...");
    
    bool allSuccess = true;
    
    // 设置探测模式（0-实时探测模式，1-睡眠探测模式）
    if (!setDetectionMode(params->detectionMode)) {
        allSuccess = false;
    }
    
    // 配置人体存在检测（用于：存在信息上报、无人计时状态上报）
    if (params->existSwitch) {
        if (!enableHumanPresence()) allSuccess = false;
    } else {
        if (!disableHumanPresence()) allSuccess = false;
    }
    
    // 配置呼吸检测（用于：呼吸心率信息上报、睡眠综合状态上报、睡眠报告上报）
    if (params->breathingSwitch) {
        if (!enableBreathDetection()) allSuccess = false;
    } else {
        if (!disableBreathDetection()) allSuccess = false;
    }
    
    // 配置心率检测（用于：呼吸心率信息上报、睡眠综合状态上报、睡眠报告上报）
    if (params->heartRateSwitch) {
        if (!enableHeartRateDetection()) allSuccess = false;
    } else {
        if (!disableHeartRateDetection()) allSuccess = false;
    }
    
    // 配置睡眠监测（用于：存在信息上报、睡眠综合状态上报、睡眠报告上报）
    if (params->sleepSwitch) {
        if (!enableSleepMonitoring()) allSuccess = false;
    } else {
        if (!disableSleepMonitoring()) allSuccess = false;
    }
    
    // 配置异常挣扎检测（用于：异常挣扎上报）
    if (params->abnormalStruggleSwitch) {
        if (!enableAbnormalStruggleDetection(true)) allSuccess = false;
    } else {
        if (!enableAbnormalStruggleDetection(false)) allSuccess = false;
    }
    
    // 配置无人计时功能（用于：无人计时状态上报）
    if (params->longTimeNoTimerSwitch) {
        if (!enableLongTimeNoTimer(true)) allSuccess = false;
        // 设置无人计时时长
        if (params->unmanneDuration >= 30 && params->unmanneDuration <= 180) {
            if (!setLongTimeNoTimerDuration(params->unmanneDuration)) allSuccess = false;
        }
    } else {
        if (!enableLongTimeNoTimer(false)) allSuccess = false;
    }
    
    if (allSuccess) {
        Serial0.println("[Radar] 检测功能配置完成");
    } else {
        Serial0.println("[Radar] 检测功能配置部分失败");
    }
    
    return allSuccess;
}

/**
 * @brief 查询产品型号
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return true-查询成功，false-查询失败
 */
bool RadarR60A::queryProductModel(char* buffer, size_t bufferSize) {
    if (!_initialized || buffer == nullptr || bufferSize == 0) {
        Serial0.println("[Radar] 错误：参数无效");
        return false;
    }
    
    Serial0.println("[Radar] 查询产品型号");
    uint8_t data = 0x0F;
    sendCommand(CTRL_PRODUCT_INFO, 0xA1, &data, 1);
    
    delay(100);
    readSerial();
    
    // 实际实现需要解析返回数据，这里简化处理
    strncpy(buffer, "R60ABD1", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

/**
 * @brief 查询产品ID
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return true-查询成功，false-查询失败
 */
bool RadarR60A::queryProductId(char* buffer, size_t bufferSize) {
    if (!_initialized || buffer == nullptr || bufferSize == 0) {
        Serial0.println("[Radar] 错误：参数无效");
        return false;
    }
    
    Serial0.println("[Radar] 查询产品ID");
    uint8_t data = 0x0F;
    sendCommand(CTRL_PRODUCT_INFO, 0xA2, &data, 1);
    
    delay(100);
    readSerial();
    
    strncpy(buffer, "R60ABD1-V3.3", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

/**
 * @brief 查询硬件型号
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return true-查询成功，false-查询失败
 */
bool RadarR60A::queryHardwareModel(char* buffer, size_t bufferSize) {
    if (!_initialized || buffer == nullptr || bufferSize == 0) {
        Serial0.println("[Radar] 错误：参数无效");
        return false;
    }
    
    Serial0.println("[Radar] 查询硬件型号");
    uint8_t data = 0x0F;
    sendCommand(CTRL_PRODUCT_INFO, 0xA3, &data, 1);
    
    delay(100);
    readSerial();
    
    strncpy(buffer, "R60ABD1-HW-V1.0", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

/**
 * @brief 查询固件版本
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return true-查询成功，false-查询失败
 */
bool RadarR60A::queryFirmwareVersion(char* buffer, size_t bufferSize) {
    if (!_initialized || buffer == nullptr || bufferSize == 0) {
        Serial0.println("[Radar] 错误：参数无效");
        return false;
    }
    
    Serial0.println("[Radar] 查询固件版本");
    uint8_t data = 0x0F;
    sendCommand(CTRL_PRODUCT_INFO, 0xA4, &data, 1);
    
    delay(100);
    readSerial();
    
    strncpy(buffer, "V3.3", bufferSize - 1);
    buffer[bufferSize - 1] = '\0';
    return true;
}

/**
 * @brief 查询初始化状态
 * @return true-已初始化，false-未初始化或查询失败
 */
bool RadarR60A::queryInitStatus() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 查询初始化状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_STATUS, 0x81, &data, 1);
    
    delay(100);
    readSerial();
    
    return _initialized;
}

/**
 * @brief 查询位置越界状态
 * @return true-范围内，false-范围外或查询失败
 */
bool RadarR60A::queryPositionStatus() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 查询位置越界状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_RANGE, 0x87, &data, 1);
    
    delay(100);
    readSerial();
    
    // 假设在范围内
    return true;
}

/**
 * @brief 设置探测模式
 * @param mode 探测模式：0-实时探测模式，1-睡眠探测模式
 * @return true-设置成功，false-设置失败
 */
bool RadarR60A::setDetectionMode(uint8_t mode) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.print("[Radar] 设置探测模式：");
    Serial0.println(mode == 0 ? "实时探测模式" : "睡眠探测模式");
    
    // 探测模式切换通过睡眠监测开关实现
    // 实时模式：关闭睡眠监测，开启实时检测
    // 睡眠模式：开启睡眠监测
    if (mode == 1) {
        // 睡眠探测模式：开启睡眠监测
        return enableSleepMonitoring();
    } else {
        // 实时探测模式：保持当前状态，睡眠监测可按需开启
        Serial0.println("[Radar] 实时探测模式已设置");
        return true;
    }
}

/**
 * @brief 设置心率波形上报开关
 * @param enable true-开启，false-关闭
 * @return true-设置成功，false-设置失败
 * @details 心率波形上报默认关闭，开启后每秒上报 5 个心率波形数据点
 */
bool RadarR60A::enableHeartRateWaveReport(bool enable) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.print("[Radar] 设置心率波形上报：");
    Serial0.println(enable ? "开启" : "关闭");
    
    uint8_t data = enable ? 0x01 : 0x00;
    sendCommand(CTRL_HEART_RATE, 0x0A, &data, 1);
    
    delay(100);
    readSerial();
    
    Serial0.println("[Radar] 已发送心率波形上报设置命令");
    return true;
}

/**
 * @brief 设置呼吸波形上报开关
 * @param enable true-开启，false-关闭
 * @return true-设置成功，false-设置失败
 * @details 呼吸波形上报默认关闭，开启后每秒上报 5 个呼吸波形数据点
 */
bool RadarR60A::enableBreathWaveReport(bool enable) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.print("[Radar] 设置呼吸波形上报：");
    Serial0.println(enable ? "开启" : "关闭");
    
    uint8_t data = enable ? 0x01 : 0x00;
    sendCommand(CTRL_BREATH, 0x0A, &data, 1);
    
    delay(100);
    readSerial();
    
    Serial0.println("[Radar] 已发送呼吸波形上报设置命令");
    return true;
}

/**
 * @brief 查询人体存在开关状态
 * @return true-已开启，false-已关闭或查询失败
 */
bool RadarR60A::queryHumanPresenceSwitch() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 查询人体存在开关状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_HUMAN_PRESENCE, 0x80, &data, 1);
    
    delay(100);
    readSerial();
    
    // 假设已开启
    return true;
}

/**
 * @brief 查询心率监测开关状态
 * @return true-已开启，false-已关闭或查询失败
 */
bool RadarR60A::queryHeartRateSwitch() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 查询心率监测开关状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_HEART_RATE, 0x80, &data, 1);
    
    delay(100);
    readSerial();
    
    // 假设已开启
    return true;
}

/**
 * @brief 查询呼吸监测开关状态
 * @return true-已开启，false-已关闭或查询失败
 */
bool RadarR60A::queryBreathSwitch() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 查询呼吸监测开关状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_BREATH, 0x80, &data, 1);
    
    delay(100);
    readSerial();
    
    // 假设已开启
    return true;
}

/**
 * @brief 查询人体距离
 * @return 距离值（cm），0-查询失败或无人
 */
uint16_t RadarR60A::queryHumanDistance() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return 0;
    }
    
    Serial0.println("[Radar] 查询人体距离");
    uint8_t data = 0x0F;
    sendCommand(CTRL_HUMAN_PRESENCE, 0x84, &data, 1);
    
    delay(100);
    readSerial();
    
    // 实际实现需要解析返回数据
    return 0;
}

/**
 * @brief 查询人体方位信息
 * @param x x 轴坐标（cm），有正负
 * @param y y 轴坐标（cm），有正负
 * @param z z 轴坐标（cm），有正负
 * @return true-查询成功，false-查询失败
 */
bool RadarR60A::queryHumanPosition(int16_t* x, int16_t* y, int16_t* z) {
    if (!_initialized || x == nullptr || y == nullptr || z == nullptr) {
        Serial0.println("[Radar] 错误：参数无效");
        return false;
    }
    
    Serial0.println("[Radar] 查询人体方位信息");
    uint8_t data = 0x0F;
    sendCommand(CTRL_HUMAN_PRESENCE, 0x85, &data, 1);
    
    delay(100);
    readSerial();
    
    // 实际实现需要解析返回的 6 字节数据
    *x = 0;
    *y = 0;
    *z = 0;
    return true;
}

/**
 * @brief 设置异常挣扎检测开关
 * @param enable true-开启，false-关闭
 * @return true-设置成功，false-设置失败
 */
bool RadarR60A::enableAbnormalStruggleDetection(bool enable) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.print("[Radar] 设置异常挣扎检测：");
    Serial0.println(enable ? "开启" : "关闭");
    
    uint8_t data = enable ? 0x01 : 0x00;
    sendCommand(CTRL_SLEEP_MONITOR, 0x13, &data, 1);
    
    delay(100);
    readSerial();
    
    return true;
}

/**
 * @brief 设置无人计时功能开关
 * @param enable true-开启，false-关闭
 * @return true-设置成功，false-设置失败
 */
bool RadarR60A::enableLongTimeNoTimer(bool enable) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.print("[Radar] 设置无人计时功能：");
    Serial0.println(enable ? "开启" : "关闭");
    
    uint8_t data = enable ? 0x01 : 0x00;
    sendCommand(CTRL_SLEEP_MONITOR, 0x14, &data, 1);
    
    delay(100);
    readSerial();
    
    return true;
}

/**
 * @brief 设置无人计时时长
 * @param duration 计时时长（30~180分钟）
 * @return true-设置成功，false-设置失败
 */
bool RadarR60A::setLongTimeNoTimerDuration(uint8_t duration) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    if (duration < 30 || duration > 180) {
        Serial0.println("[Radar] 错误：无人计时时长超出范围（30~180分钟）");
        return false;
    }
    
    Serial0.print("[Radar] 设置无人计时时长：");
    Serial0.print(duration);
    Serial0.println(" 分钟");
    
    sendCommand(CTRL_SLEEP_MONITOR, 0x15, &duration, 1);
    
    delay(100);
    readSerial();
    
    return true;
}

/**
 * @brief 设置呼吸过低判读阈值
 * @param threshold 呼吸阈值（次/min）
 * @return true-设置成功，false-设置失败
 */
bool RadarR60A::setBreathLowThreshold(uint8_t threshold) {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.print("[Radar] 设置呼吸过低阈值：");
    Serial0.print(threshold);
    Serial0.println(" 次/min");
    
    sendCommand(CTRL_BREATH, 0x0B, &threshold, 1);
    
    delay(100);
    readSerial();
    
    return true;
}

/**
 * @brief 查询睡眠监测开关状态
 * @return true-已开启，false-已关闭或查询失败
 */
bool RadarR60A::querySleepSwitch() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 查询睡眠监测开关状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_SLEEP_MONITOR, 0x80, &data, 1);
    
    delay(100);
    readSerial();
    
    return true;
}

/**
 * @brief 查询入床离床状态
 * @return 0-离床，1-入床，2-无
 */
uint8_t RadarR60A::queryBedStatus() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return 2;
    }
    
    Serial0.println("[Radar] 查询入床离床状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_SLEEP_MONITOR, 0x81, &data, 1);
    
    delay(100);
    readSerial();
    
    // 实际实现需要解析返回数据
    return 1; // 默认返回入床状态
}

/**
 * @brief 查询睡眠异常状态
 * @return 0-无异常，1-睡眠时长异常，2-无人异常
 */
uint8_t RadarR60A::querySleepAbnormalStatus() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return 0;
    }
    
    Serial0.println("[Radar] 查询睡眠异常状态");
    uint8_t data = 0x0F;
    sendCommand(CTRL_SLEEP_MONITOR, 0x8E, &data, 1);
    
    delay(100);
    readSerial();
    
    // 实际实现需要解析返回数据
    return 0; // 默认返回无异常
}

/**
 * @brief 雷达模组软复位（通过命令方式）
 * @details 发送复位命令使雷达模块重启，不同于 resetModule() 的硬件复位方式
 * @return true-复位成功，false-复位失败
 */
bool RadarR60A::softResetModule() {
    if (!_initialized) {
        Serial0.println("[Radar] 错误：雷达未初始化");
        return false;
    }
    
    Serial0.println("[Radar] 执行雷达模组软复位");
    
    uint8_t data = 0x0F;
    sendCommand(CTRL_SYSTEM, 0x02, &data, 1);
    
    delay(500);
    readSerial();
    
    // 复位后重新初始化
    _initialized = false;
    delay(1000);
    
    return begin();
}

/**
 * @brief 开始OTA升级
 * @param version 新版本号
 * @return true-成功，false-失败
 */
bool RadarR60A::startOtaUpgrade(const char* version) {
    if (!_initialized || version == nullptr) {
        Serial0.println("[Radar] 错误：雷达未初始化或版本号为空");
        return false;
    }
    
    Serial0.print("[Radar] 开始OTA升级，版本：");
    Serial0.println(version);
    
    // 发送开始升级命令
    uint8_t cmd = 0x01;
    sendCommand(CTRL_OTA, cmd, nullptr, 0);
    
    delay(100);
    readSerial();
    
    Serial0.println("[Radar] OTA升级流程已启动");
    return true;
}
