/**
 * @file A7670C.cpp
 * @brief A7670C 4G LTE 模块驱动实现
 * @details 本文件实现了基于合宙 A7670C 4G 模块的网络通信功能，包括：
 *          - 模块电源控制
 *          - AT 命令发送与响应解析
 *          - 网络注册与 GPRS 附着
 *          - TCP 连接建立与数据发送
 *          - 信号强度、ICCID、IMEI 查询
 *          实现了 NetworkInterface 接口，可被 NetworkManager 统一管理。
 */

#include "A7670C.h"

/**
 * @brief 构造函数
 * @param rxPin ESP32 接收引脚（默认 16）
 * @param txPin ESP32 发送引脚（默认 17）
 * @param pwrKeyPin 电源控制引脚（默认 18）
 * @param baudRate 串口波特率（默认 115200）
 * @details 初始化引脚配置和成员变量，_serial 在 begin() 中动态创建
 */
A7670C::A7670C(int rxPin, int txPin, int pwrKeyPin, long baudRate) {
    _rxPin = rxPin;
    _txPin = txPin;
    _pwrKeyPin = pwrKeyPin;
    _baudRate = baudRate;
    _serial = nullptr;
    _tcpConnected = false;
    _port = 0;
    memset(_apn, 0, sizeof(_apn));
    memset(_server, 0, sizeof(_server));
}

/**
 * @brief 析构函数
 * @details 依次执行：
 *          1. 调用 disconnect() 断开 TCP 连接
 *          2. 删除动态创建的 HardwareSerial 对象
 */
A7670C::~A7670C() {
    disconnect();
    if (_serial != nullptr) {
        delete _serial;
        _serial = nullptr;
    }
}

/**
 * @brief 初始化 4G 模块
 * @return true-成功，false-失败
 * @details 完整的初始化流程包括： 
 *          1. 创建 HardwareSerial 对象并配置串口参数
 *          2. 配置电源控制引脚为输出，初始为低电平
 *          3. 调用 powerOn() 启动模块
 *          4. 等待模块启动完成（2 秒）
 *          5. 发送 AT 命令测试通信
 *          6. 查询固件版本、SIM 卡状态、网络注册状态
 */
bool A7670C::begin() {
    if (_serial == nullptr) {
        _serial = new HardwareSerial(1);
        _serial->begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
    }
    
    pinMode(_pwrKeyPin, OUTPUT);
    digitalWrite(_pwrKeyPin, LOW);
    
    if (!powerOn()) {
        return false;
    }
    
    delay(2000);
    
    if (!sendATCommand("AT")) {
        return false;
    }
    
    sendATCommand("AT+CGMR");
    sendATCommand("AT+CPIN?");
    sendATCommand("AT+CREG?");
    
    _status = NETWORK_DISCONNECTED;
    return true;
}

/**
 * @brief 断开 4G 网络连接
 * @details 如果 TCP 已连接，发送 "AT+CIPCLOSE=0" 关闭连接，
 *          然后设置状态为 NETWORK_DISCONNECTED
 */
void A7670C::disconnect() {
    if (_tcpConnected) {
        sendATCommand("AT+CIPCLOSE=0");
        _tcpConnected = false;
    }
    _status = NETWORK_DISCONNECTED;
}

/**
 * @brief 获取 4G 网络连接状态
 * @return NetworkStatus 枚举值
 */
NetworkStatus A7670C::getStatus() {
    return _status;
}

/**
 * @brief 通过 TCP 连接发送数据
 * @param data 数据指针
 * @param length 数据长度
 * @return true-发送成功，false-失败或未连接
 * @details 发送流程：
 *          1. 检查 TCP 连接状态
 *          2. 发送 "AT+CIPSEND=0,length" 命令
 *          3. 等待模块返回 ">" 提示符
 *          4. 发送实际数据
 *          5. 等待模块返回 "SEND OK" 确认
 */
bool A7670C::send(const uint8_t* data, uint16_t length) {
    if (!_tcpConnected) {
        return false;
    }
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%d", length);
    if (!sendATCommand(cmd, ">")) {
        return false;
    }
    
    _serial->write(data, length);
    return waitForResponse("SEND OK");
}

/**
 * @brief 周期性更新函数
 * @details 在主循环中调用，读取串口接收的数据，
 *          逐字节触发数据回调。适用于接收服务器下发的命令或数据。
 */
void A7670C::update() {
    if (_serial == nullptr) return;
    
    while (_serial->available()) {
        char c = _serial->read();
        if (_dataCallback != nullptr) {
            uint8_t byte = (uint8_t)c;
            _dataCallback(&byte, 1, _userData);
        }
    }
}

/**
 * @brief 设置 APN（接入点名称）
 * @param apn APN 字符串
 * @details APN 用于 4G 网络接入，常见 APN：
 *          - 中国移动：cmnet
 *          - 中国联通：3gnet
 *          - 中国电信：ctnet
 */
void A7670C::setAPN(const char* apn) {
    if (apn != nullptr) {
        strncpy(_apn, apn, sizeof(_apn) - 1);
    }
}

/**
 * @brief 建立 TCP 连接到服务器
 * @param server 服务器 IP 或域名
 * @param port 服务器端口
 * @return true-连接成功，false-失败
 * @details 连接流程：
 *          1. 如果配置了 APN，发送 "AT+CGSOCKCONT" 设置 APN
 *          2. 发送 "AT+CIPMODE=0" 设置非透传模式
 *          3. 发送 "AT+CGATT=1" 附着 GPRS
 *          4. 等待 3 秒让网络稳定
 *          5. 发送 "AT+CREG?" 查询网络注册状态
 *          6. 发送 "AT+CIPSTART" 建立 TCP 连接
 *          7. 等待 "CONNECT OK" 响应
 */
bool A7670C::connectTCP(const char* server, uint16_t port) {
    if (server == nullptr || strlen(server) == 0 || port == 0) {
        return false;
    }
    
    strncpy(_server, server, sizeof(_server) - 1);
    _port = port;
    
    triggerStatusCallback(NETWORK_CONNECTING, "连接网络");
    
    if (strlen(_apn) > 0) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "AT+CGSOCKCONT=1,\"IP\",\"%s\"", _apn);
        sendATCommand(cmd);
    }
    
    sendATCommand("AT+CIPMODE=0");
    sendATCommand("AT+CGATT=1");
    
    delay(3000);
    
    sendATCommand("AT+CREG?");
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=0,\"TCP\",\"%s\",%d", _server, _port);
    
    if (sendATCommand(cmd, "CONNECT OK")) {
        _tcpConnected = true;
        _status = NETWORK_CONNECTED;
        triggerStatusCallback(NETWORK_CONNECTED, "TCP 连接成功");
        return true;
    } else {
        triggerStatusCallback(NETWORK_ERROR, "TCP 连接失败");
        return false;
    }
}

/**
 * @brief 启动 4G 模块电源
 * @return true-启动成功，false-超时
 * @details 电源启动流程：
 *          1. 拉高 PWRKEY 引脚
 *          2. 保持 1.5 秒
 *          3. 拉低 PWRKEY 引脚
 *          4. 等待 3 秒让模块启动
 *          5. 等待模块返回 "RDY" 就绪信号（超时 10 秒）
 */
bool A7670C::powerOn() {
    digitalWrite(_pwrKeyPin, HIGH);
    delay(1500);
    digitalWrite(_pwrKeyPin, LOW);
    delay(3000);
    
    return waitForResponse("RDY", 10000);
}

/**
 * @brief 关闭 4G 模块电源
 * @return true-成功
 * @details 发送 "AT+CPOWD=1" 命令关闭模块
 */
bool A7670C::powerOff() {
    sendATCommand("AT+CPOWD=1");
    return true;
}

/**
 * @brief 等待串口响应
 * @param expected 期望的响应字符串
 * @param timeout 超时时间（毫秒，默认 5000）
 * @return true-在超时前收到期望响应，false-超时
 * @details 循环读取串口数据，累积到缓冲区中，
 *          使用 strstr() 查找期望的响应字符串。
 *          适用于 AT 命令响应验证。
 */
bool A7670C::waitForResponse(const char* expected, unsigned long timeout) {
    if (_serial == nullptr) return false;
    
    unsigned long startTime = millis();
    char buffer[512];
    int idx = 0;
    
    while (millis() - startTime < timeout) {
        if (_serial->available()) {
            char c = _serial->read();
            if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = c;
                buffer[idx] = '\0';
            }
            
            if (strstr(buffer, expected) != nullptr) {
                return true;
            }
        }
        delay(10);
    }
    
    return false;
}

/**
 * @brief 发送 AT 命令并等待响应
 * @param cmd AT 命令字符串
 * @param expected 期望的响应字符串（默认 "OK"）
 * @param timeout 超时时间（毫秒，默认 5000）
 * @return true-收到期望响应，false-超时
 * @details 发送命令后自动添加换行符，然后调用 waitForResponse()
 *          等待期望的响应。若 expected 为 nullptr，则默认等待 "OK"。
 */
bool A7670C::sendATCommand(const char* cmd, const char* expected, unsigned long timeout) {
    if (_serial == nullptr || cmd == nullptr) {
        return false;
    }
    
    _serial->print(cmd);
    _serial->println();
    
    return waitForResponse(expected != nullptr ? expected : "OK", timeout);
}

/**
 * @brief 获取信号强度
 * @return 信号强度值（0-31），-1 表示获取失败
 * @details 发送 "AT+CSQ" 命令查询信号质量，
 *          解析响应中的 RSSI 值。
 *          RSSI 值映射：
 *          - 0: -113 dBm 或更低
 *          - 1: -111 dBm
 *          - 2..30: -109..-53 dBm
 *          - 31: -51 dBm 或更高
 *          - 99: 未知或不可检测
 */
int A7670C::getSignalStrength() {
    if (_serial == nullptr) return -1;
    
    _serial->println("AT+CSQ");
    
    unsigned long startTime = millis();
    char buffer[128];
    int idx = 0;
    
    while (millis() - startTime < 3000) {
        if (_serial->available()) {
            char c = _serial->read();
            if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = c;
                buffer[idx] = '\0';
            }
            
            if (strstr(buffer, "+CSQ:") != nullptr) {
                int rssi, ber;
                if (sscanf(buffer, "+CSQ: %d,%d", &rssi, &ber) == 2) {
                    if (rssi == 99 || rssi == 199) return -1;
                    return rssi;
                }
            }
        }
        delay(10);
    }
    
    return -1;
}

/**
 * @brief 获取 SIM 卡 ICCID（集成电路卡识别码）
 * @param buffer 存储 ICCID 的缓冲区（至少 21 字节）
 * @param bufferSize 缓冲区大小
 * @return true-成功获取，false-失败
 * @details 发送 "AT+CCID" 命令查询 SIM 卡 ICCID，
 *          解析响应中的 ICCID 字符串（20 位数字）
 */
bool A7670C::getICCID(char* buffer, size_t bufferSize) {
    if (_serial == nullptr || buffer == nullptr || bufferSize < 21) return false;
    
    _serial->println("AT+CCID");
    
    unsigned long startTime = millis();
    char tempBuffer[256];
    int idx = 0;
    
    while (millis() - startTime < 3000) {
        if (_serial->available()) {
            char c = _serial->read();
            if (idx < sizeof(tempBuffer) - 1) {
                tempBuffer[idx++] = c;
                tempBuffer[idx] = '\0';
            }
            
            if (strstr(tempBuffer, "+CCID:") != nullptr) {
                char* start = strstr(tempBuffer, "+CCID: ");
                if (start != nullptr) {
                    start += 7;
                    strncpy(buffer, start, bufferSize - 1);
                    buffer[bufferSize - 1] = '\0';
                    return true;
                }
            }
        }
        delay(10);
    }
    
    return false;
}

/**
 * @brief 获取 IMEI（国际移动设备识别码）
 * @param buffer 存储 IMEI 的缓冲区（至少 16 字节）
 * @param bufferSize 缓冲区大小
 * @return true-成功获取，false-失败
 * @details 发送 "AT+GSN" 命令查询 IMEI，
 *          解析响应中的 IMEI 字符串（15 位数字）
 */
bool A7670C::getIMEI(char* buffer, size_t bufferSize) {
    if (_serial == nullptr || buffer == nullptr || bufferSize < 16) return false;
    
    _serial->println("AT+GSN");
    
    unsigned long startTime = millis();
    char tempBuffer[128];
    int idx = 0;
    
    while (millis() - startTime < 3000) {
        if (_serial->available()) {
            char c = _serial->read();
            if (idx < sizeof(tempBuffer) - 1) {
                tempBuffer[idx++] = c;
                tempBuffer[idx] = '\0';
            }
            
            char* end = strstr(tempBuffer, "\r\nOK\r\n");
            if (end != nullptr) {
                *end = '\0';
                strncpy(buffer, tempBuffer, bufferSize - 1);
                buffer[bufferSize - 1] = '\0';
                return true;
            }
        }
        delay(10);
    }
    
    return false;
}
