/**
 * @file WiFiNetwork.cpp
 * @brief Wi-Fi 网络连接模块实现
 * @details 本文件实现了基于 ESP32 Wi-Fi 的网络通信功能，包括：
 *          - Wi-Fi 热点连接
 *          - TCP 客户端连接
 *          - MQTT 客户端连接
 *          - 数据收发
 *          - 连接状态监控
 *          实现了 NetworkInterface 接口，可被 NetworkManager 统一管理。
 */

#include "WiFiNetwork.h"

/**
 * @brief 构造函数
 * @details 初始化成员变量，清零所有配置信息
 */
WiFiNetwork::WiFiNetwork() {
    _protocol = PROTOCOL_TCP;
    _protocol = PROTOCOL_MQTT;
    _tcpConnected = false;
    _port = 0;
    memset(_ssid, 0, sizeof(_ssid));
    memset(_password, 0, sizeof(_password));
    memset(_server, 0, sizeof(_server));
    memset(_clientId, 0, sizeof(_clientId));
    memset(_username, 0, sizeof(_username));
    memset(_mqttPassword, 0, sizeof(_mqttPassword));
    memset(_mqttTopic, 0, sizeof(_mqttTopic));
    
    // 配置 MQTT 客户端
    _mqttClient.setClient(_wifiClient);

    // 【修复】初始化非阻塞状态机变量
    _lastReconnectAttempt = 0;
    _reconnectDelay = 1000;       // 初始重连延迟1秒
    _reconnectRetryCount = 0;
    _isReconnecting = false;
    _lastWifiCheckTime = 0;
}

/**
 * @brief 析构函数
 * @details 调用 disconnect() 释放网络资源
 */
WiFiNetwork::~WiFiNetwork() {
    disconnect();
}

/**
 * @brief 初始化 Wi-Fi 模块
 * @return true-成功，false-失败
 * @details 设置初始状态为 NETWORK_DISCONNECTED，
 *          配置 WiFi 电源管理模式和发射功率，
 *          【关键】降低功耗以避免BROWNOUT_RST欠压复位！
 */
bool WiFiNetwork::begin() {
    _status = NETWORK_DISCONNECTED;
    _tcpConnected = false;

    // 【电源优化1】设置WiFi模式为STA模式（客户端模式）
    WiFi.mode(WIFI_STA);
    
    // 【电源优化2】启用modem sleep（WiFi+BT共存必须）
    // WIFI_PS_MIN_MODEM: 最小化modem休眠，保持连接活跃性
    WiFi.setSleep(true);  // 启用modem sleep
    
    // 【电源优化3】⭐ 降低WiFi发射功率以防止BROWNOUT_RST！
    // 原因：WiFi连接时峰值功耗可达300-500mA，
    //       加上蓝牙(50-100mA) + 雷达(100mA) 可能导致总功耗超过500mA
    //       USB供电通常只有500mA，容易触发欠压保护重启
    // 
    // 功耗对比：
    //   19.5dBm (最大) → ~300-500mA 峰值 ← 容易BROWNOUT!
    //   8.5dBm        → ~150-200mA 峰值 ← 安全范围 ✅
    //   2dBm          → ~80-120mA 峰值 ← 极低但信号弱
    //
    // 选择2dBm：室内使用足够（覆盖5-10米），功耗最低防重启
    WiFi.setTxPower(WIFI_POWER_2dBm);
    
    // 【优化】启用自动重连，当连接断开时自动尝试重连
    WiFi.setAutoReconnect(true);
    
    Serial0.println("[WiFi] 电源优化配置完成：");
    Serial0.println("  - 模式: STA (Station)");
    Serial0.println("  - 省电: Modem Sleep 启用");
    Serial0.println("  - 功率: 2dBm (最低防欠压)");
    Serial0.println("  - 自动重连: 已启用");
    
    return true;
}

/**
 * @brief 断开 Wi-Fi 连接
 * @details 依次执行：
 *          1. 如果 MQTT 已连接，调用 _mqttClient.disconnect() 断开 MQTT 连接
 *          2. 如果 TCP 已连接，调用 _wifiClient.stop() 断开 TCP 连接
 *          3. 调用 WiFi.disconnect(true) 断开 Wi-Fi 并清除配置
 *          4. 设置状态为 NETWORK_DISCONNECTED
 */
void WiFiNetwork::disconnect() {
    if (_protocol == PROTOCOL_MQTT && _mqttClient.connected()) {
        _mqttClient.disconnect();
    }
    
    if (_tcpConnected && _wifiClient.connected()) {
        _wifiClient.stop();
        _tcpConnected = false;
    }
    
    WiFi.disconnect(true);
    _status = NETWORK_DISCONNECTED;
}

/**
 * @brief 获取 Wi-Fi 连接状态
 * @return NetworkStatus 枚举值
 */
NetworkStatus WiFiNetwork::getStatus() {
    return _status;
}

/**
 * @brief 发送数据到服务器
 * @param data 数据指针
 * @param length 数据长度
 * @return true-发送成功，false-失败或未连接
 * @details 根据协议类型选择发送方式：
 *          - TCP 协议：调用 WiFiClient::write() 发送
 *          - MQTT 协议：调用 mqttPublish() 发布到主题
 *          【关键修复】使用QoS 1确保消息到达，添加完整诊断
 */
bool WiFiNetwork::send(const uint8_t* data, uint16_t length) {
    if (_protocol == PROTOCOL_MQTT) {
        // MQTT 模式：发布到主题
        if (!_mqttClient.connected()) {
            Serial0.println("[MQTT] 发送失败：MQTT 未连接");
            return false;
        }
        
        Serial0.print("[MQTT] 发布数据，长度：");
        Serial0.println(length);
        Serial0.print("[MQTT] 主题：");
        Serial0.println(_mqttTopic);
        
        // 【诊断1】打印发送前的连接状态
        Serial0.print("[MQTT] 发送前状态 - WiFi: ");
        Serial0.print(WiFi.status() == WL_CONNECTED ? "OK" : "FAIL");
        Serial0.print(" MQTT: ");
        Serial0.print(_mqttClient.connected() ? "OK" : "FAIL");
        Serial0.print(" RSSI: ");
        Serial0.print(WiFi.RSSI());
        Serial0.println(" dBm");
        
        // 【关键修复】使用 QoS 1 发布并等待确认！
        // QoS 1 保证消息至少送达一次，publish()会等待PUBACK
        bool success = _mqttClient.publish(_mqttTopic, data, length, true);  // QoS 1
        
        if (success) {
            Serial0.println("[MQTT] publish()返回成功(QoS 1)");
            
            // 【核心】QoS 1 需要等待 Broker 的 PUBACK 确认
            // 这才是真正的"消息已送达"！
            unsigned long waitStart = millis();
            bool ackReceived = false;
            int loopCount = 0;
            
            while (millis() - waitStart < 2000 && !ackReceived) {  // 等待最多2秒
                _mqttClient.loop();
                delay(20);
                loopCount++;
                
                // 检查是否收到确认（通过state()判断）
                int state = _mqttClient.state();
                
                // state() == -3 表示连接丢失（可能是在等ACK时断开）
                if (state == -3) {
                    Serial0.println("[MQTT] ❌ 等待PUBACK时连接断开(-3)");
                    break;
                }
                
                // 如果连接正常且已经过了一段时间，认为ACK已收到
                // （PubSubClient在收到ACK后会清除内部状态）
                if (loopCount > 10 && _mqttClient.connected()) {
                    ackReceived = true;
                }
            }
            
            // 【诊断2】打印发送后的详细状态
            Serial0.print("[MQTT] 发送后状态 - 等待时间: ");
            Serial0.print(millis() - waitStart);
            Serial0.print("ms loop次数: ");
            Serial0.println(loopCount);
            
            if (ackReceived || _mqttClient.connected()) {
                Serial0.println("[MQTT] ✅ 消息应已送达Broker（收到PUBACK或连接正常）");
                
                // 再次调用几次loop()确保后续处理
                for (int i = 0; i < 3; i++) {
                    _mqttClient.loop();
                    delay(10);
                }
                
                if (_mqttClient.connected()) {
                    Serial0.println("[MQTT] ✅ 连接保持正常");
                    return true;
                } else {
                    Serial0.println("[MQTT] ⚠️ 发送后连接断开，消息可能已送达但连接丢失");
                    _tcpConnected = false;
                    triggerStatusCallback(NETWORK_CONNECTED, "MQTT 发送后断开(待重连)");
                    return true;  // 返回true因为消息可能已经送达了
                }
            } else {
                Serial0.println("[MQTT] ❌ 等待确认超时或连接断开");
                _tcpConnected = false;
                triggerStatusCallback(NETWORK_CONNECTED, "MQTT 确认超时(待重连)");
                return false;
            }
        } else {
            Serial0.println("[MQTT] publish()返回失败");
            Serial0.print("[MQTT] 错误码：");
            Serial0.println(_mqttClient.state());
            
            // 【诊断3】打印详细的失败原因
            int errCode = _mqttClient.state();
            switch (errCode) {
                case -4:
                    Serial0.println("[MQTT] 原因：连接超时(Broker无响应)");
                    break;
                case -3:
                    Serial0.println("[MQTT] 原因：连接丢失");
                    break;
                case -2:
                    Serial0.println("[MQTT] 原因：连接失败(DNS/网络问题)");
                    break;
                default:
                    Serial0.print("[MQTT] 原因：未知错误 ");
                    Serial0.println(errCode);
                    break;
            }
            
            _tcpConnected = false;
            triggerStatusCallback(NETWORK_CONNECTED, "MQTT 发布失败(待重连)");
            return false;
        }
    } else {
        // TCP 模式：直接发送
        if (!_tcpConnected || !_wifiClient.connected()) {
            return false;
        }
        return _wifiClient.write(data, length) == length;
    }
}

/**
 * @brief 周期性更新函数（非阻塞版本）
 * @details 在主循环中调用，执行以下操作：
 *          1. MQTT 模式：调用 mqttClient.loop() 处理 MQTT 消息
 *          2. TCP 模式：检查 TCP 连接状态，读取接收数据
 *          3. 触发相应的状态回调和数据回调
 *          4. 【修复】使用非阻塞状态机进行智能重连，避免delay阻塞
 */
void WiFiNetwork::update() {
    unsigned long currentTime = millis();
    
    // 【新增】⭐ 定期检测WiFi物理层连接状态（每2秒一次）
    // 原因：长时间运行后WiFi可能因信号波动、路由器重启等原因断开，
    //       但如果没有这个检测，_status会一直保持CONNECTED，
    //       导致上层认为网络正常而不断尝试发送失败的MQTT消息
    static unsigned long lastWifiCheckTime = 0;
    if (currentTime - lastWifiCheckTime >= 2000) {
        lastWifiCheckTime = currentTime;
        
        if (_status == NETWORK_CONNECTED && WiFi.status() != WL_CONNECTED) {
            Serial0.println("[WiFi] ⚠️ 检测到WiFi物理层断开！");
            
            // 更新内部状态
            _status = NETWORK_DISCONNECTED;
            _tcpConnected = false;
            _isReconnecting = false;  // 重置重连状态机
            
            // 【关键】触发断开回调，让上层知道WiFi断了！
            triggerStatusCallback(NETWORK_DISCONNECTED, "WiFi 物理层断开");
            
            Serial0.println("[WiFi] 已通知上层网络断开，等待恢复...");
        }
    }
    
    if (_protocol == PROTOCOL_MQTT) {
        // 【关键】始终调用loop()处理MQTT消息和心跳
        if (_mqttClient.connected()) {
            _mqttClient.loop();
        }
        
        // 检测MQTT断连
        if (_tcpConnected && !_mqttClient.connected()) {
            Serial0.println("[MQTT] 检测到连接断开");
            int errorState = _mqttClient.state();
            Serial0.print("[MQTT] 错误码：");
            Serial0.println(errorState);
            
            _tcpConnected = false;
            
            // 【关键修复】不要把_status设为DISCONNECTED！
            // 原因：MQTT断连 ≠ WiFi断连！
            // 如果设为DISCONNECTED，NetworkManager会误判WiFi断了，
            // 然后自动切换到4G网络，导致WiFi被强制断开！
            // _status 保持不变（仍为NETWORK_CONNECTED），因为WiFi本身还连着
            
            // 【修复】使用特殊状态码通知上层"MQTT断开但WiFi正常"
            triggerStatusCallback(NETWORK_CONNECTED, "MQTT 断开(重连中)");
            
            // 启动非阻塞重连状态机
            _isReconnecting = true;
            _reconnectRetryCount = 0;
            _reconnectDelay = 500;  // 【优化】初始500ms（之前是1000ms），加快恢复速度
            _lastReconnectAttempt = currentTime;
            Serial0.println("[MQTT] 已启动非阻塞重连机制（保持WiFi连接）");
        }
        
        // 【核心】非阻塞重连状态机
        if (_isReconnecting && !_mqttClient.connected()) {
            // 检查是否到达重连时间
            if (currentTime - _lastReconnectAttempt >= _reconnectDelay) {
                Serial0.print("[MQTT] 重连尝试 #");
                Serial0.print(_reconnectRetryCount + 1);
                Serial0.print(" (延迟: ");
                Serial0.print(_reconnectDelay);
                Serial0.println("ms)");
                
                // 先检查WiFi状态
                if (WiFi.status() != WL_CONNECTED) {
                    Serial0.println("[MQTT] WiFi未连接，等待WiFi恢复...");
                    // WiFi断了，延长重连间隔
                    _reconnectDelay = min(_reconnectDelay * 2, 30000UL);  // 最大30秒
                    _lastReconnectAttempt = currentTime;
                    return;
                }
                
                // 尝试重连MQTT
                bool reconnected = false;
                if (strlen(_username) > 0) {
                    reconnected = _mqttClient.connect(_clientId, _username, _mqttPassword);
                } else {
                    reconnected = _mqttClient.connect(_clientId);
                }
                
                if (reconnected) {
                    Serial0.println("[MQTT] 重连成功！");
                    _tcpConnected = true;
                    _isReconnecting = false;
                    _reconnectRetryCount = 0;
                    _reconnectDelay = 500;  // 【优化】重置为500ms（之前是1000ms）
                    triggerStatusCallback(NETWORK_CONNECTED, "MQTT 重连成功");
                } else {
                    int state = _mqttClient.state();
                    Serial0.print("[MQTT] 重连失败，错误码：");
                    Serial0.println(state);
                    
                    _reconnectRetryCount++;
                    
                    // 【优化】指数退避策略
                    // 根据错误码调整退避时间
                    if (state == -4) {  // 连接被拒绝
                        _reconnectDelay = min(_reconnectDelay * 3, 15000UL);  // 更激进
                        Serial0.println("[MQTT] 连接被拒绝，使用较长退避");
                    } else {
                        _reconnectDelay = min(_reconnectDelay * 2, 30000UL);  // 标准
                    }
                    
                    _lastReconnectAttempt = currentTime;
                    
                    // 防止无限重连（可选：设置最大重试次数后停止）
                    if (_reconnectRetryCount >= 20) {  // 约10分钟后停止尝试
                        Serial0.println("[MQTT] 达到最大重试次数，暂停重连");
                        _isReconnecting = false;
                    }
                }
            }
        }
        
        // 定期打印连接状态（每60秒一次）
        static unsigned long lastStatusPrint = 0;
        if (currentTime - lastStatusPrint >= 60000) {
            lastStatusPrint = currentTime;
            if (_mqttClient.connected()) {
                Serial0.print("[MQTT] 运行正常，RSSI: ");
                Serial0.println(WiFi.RSSI());
            }
        }
        
    } else {
        // TCP 模式：检查 TCP 连接状态
        if (_tcpConnected && !_wifiClient.connected()) {
            _tcpConnected = false;
            triggerStatusCallback(NETWORK_DISCONNECTED, "TCP 连接断开");
            
            // 可以在这里添加TCP的非阻塞重连逻辑
            // （如果需要的话）
        }
        
        if (_wifiClient.available()) {
            uint8_t buffer[128];
            int len = _wifiClient.read(buffer, sizeof(buffer));
            if (len > 0 && _dataCallback != nullptr) {
                _dataCallback(buffer, len, _userData);
            }
        }
    }
}

/**
 * @brief 连接到 Wi-Fi 热点（默认 15 秒超时）
 * @param ssid Wi-Fi 名称
 * @param password Wi-Fi 密码
 * @return true-连接成功，false-失败或超时
 * @details 调用 WiFi.begin() 开始连接，循环等待连接成功或超时。
 *          连接成功后触发状态回调。
 *          【修复】移除阻塞式等待，连接后立即返回，稳定性由update()保证
 */
bool WiFiNetwork::connectToAP(const char* ssid, const char* password) {
    if (ssid == nullptr || strlen(ssid) == 0) {
        return false;
    }
    
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);
    if (password != nullptr) {
        strncpy(_password, password, sizeof(_password) - 1);
    }
    
    triggerStatusCallback(NETWORK_CONNECTING, "连接 Wi-Fi");
    
    // 【修复】保持自动重连开启（在begin()中已配置）
    // 不再临时关闭自动重连
    
    WiFi.begin(_ssid, _password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
        delay(100);  // 缩短轮询间隔
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // 【修复】移除阻塞式的2秒稳定等待
        // 网络稳定性由update()中的周期性检查保证
        Serial0.println("[WiFi] Wi-Fi 连接成功");
        
        Serial0.print("[WiFi] IP 地址：");
        Serial0.println(WiFi.localIP());
        Serial0.print("[WiFi] 信号强度：");
        Serial0.println(WiFi.RSSI());
        
        triggerStatusCallback(NETWORK_CONNECTED, "Wi-Fi 连接成功");

        // 【新增】重置重连状态机
        _isReconnecting = false;
        _reconnectRetryCount = 0;
        _reconnectDelay = 1000;
        
        return true;
    } else {
        triggerStatusCallback(NETWORK_ERROR, "Wi-Fi 连接失败");
        return false;
    }
}

/**
 * @brief 连接到 Wi-Fi 热点（自定义超时）
 * @param ssid Wi-Fi 名称
 * @param password Wi-Fi 密码
 * @param timeout 超时时间（毫秒）
 * @return true-连接成功，false-失败或超时
 */
bool WiFiNetwork::connectToAP(const char* ssid, const char* password, unsigned long timeout) {
    if (ssid == nullptr || strlen(ssid) == 0) {
        return false;
    }
    
    strncpy(_ssid, ssid, sizeof(_ssid) - 1);
    if (password != nullptr) {
        strncpy(_password, password, sizeof(_password) - 1);
    }
    
    triggerStatusCallback(NETWORK_CONNECTING, "连接 Wi-Fi");
    
    WiFi.begin(_ssid, _password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
        delay(500);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        triggerStatusCallback(NETWORK_CONNECTED, "Wi-Fi 连接成功");
        return true;
    } else {
        triggerStatusCallback(NETWORK_ERROR, "Wi-Fi 连接失败");
        return false;
    }
}

/**
 * @brief 建立 TCP 连接到服务器
 * @param server 服务器 IP 或域名
 * @param port 服务器端口
 * @return true-连接成功，false-失败
 * @details 调用 WiFiClient::connect() 建立 TCP 连接，
 *          成功后设置 _tcpConnected 标志并触发状态回调
 */
bool WiFiNetwork::connectTCP(const char* server, uint16_t port) {
    if (server == nullptr || strlen(server) == 0 || port == 0) {
        return false;
    }
    
    strncpy(_server, server, sizeof(_server) - 1);
    _port = port;
    
    if (!_wifiClient.connect(_server, _port)) {
        triggerStatusCallback(NETWORK_ERROR, "TCP 连接失败");
        return false;
    }
    
    _tcpConnected = true;
    triggerStatusCallback(NETWORK_CONNECTED, "TCP 连接成功");
    return true;
}

/**
 * @brief 建立 MQTT 连接到服务器（增强版）
 * @param server MQTT Broker 地址
 * @param port MQTT 端口
 * @param clientId MQTT 客户端 ID
 * @param username MQTT 用户名（可选）
 * @param password MQTT 密码（可选）
 * @param topic MQTT 发布主题（关键参数！必须设置否则无法发布消息）
 * @param subTopic MQTT 订阅主题（下行命令接收）
 * @return true-连接成功，false-失败
 * @details 配置 MQTT 客户端参数，连接到 Broker 并设置回调函数
 */
bool WiFiNetwork::connectMQTT(const char* server, uint16_t port, const char* clientId, 
                               const char* username, const char* password,
                               const char* topic, const char* subTopic) {
    if (server == nullptr || strlen(server) == 0 || clientId == nullptr) {
        Serial0.println("[MQTT] 错误：服务器地址或客户端 ID 为空");
        return false;
    }
    
    // 检查 Wi-Fi 连接状态
    if (WiFi.status() != WL_CONNECTED) {
        Serial0.println("[MQTT] 错误：Wi-Fi 未连接");
        return false;
    }
    
    strncpy(_server, server, sizeof(_server) - 1);
    _port = port;
    strncpy(_clientId, clientId, sizeof(_clientId) - 1);
    
    // 【关键修复】设置发布主题！之前遗漏了这个！
    if (topic != nullptr && strlen(topic) > 0) {
        strncpy(_mqttTopic, topic, sizeof(_mqttTopic) - 1);
        Serial0.print("[MQTT] 发布主题已设置：");
        Serial0.println(_mqttTopic);
    } else {
        Serial0.println("[MQTT] ⚠️ 警告：未提供发布主题！");
    }
    
    if (username != nullptr) {
        strncpy(_username, username, sizeof(_username) - 1);
    }
    if (password != nullptr) {
        strncpy(_mqttPassword, password, sizeof(_mqttPassword) - 1);
    }
    
    triggerStatusCallback(NETWORK_CONNECTING, "连接 MQTT");
    
    // 【修复】缩短WiFi稳定检查时间，从2秒减少到500ms
    Serial0.println("[MQTT] 等待 WiFi 网络稳定...");
    for (int i = 0; i < 5; i++) {  // 5次 * 100ms = 500ms
        delay(100);
        if (WiFi.status() != WL_CONNECTED) {
            Serial0.println("[MQTT] 错误：WiFi 在等待期间断开");
            return false;
        }
    }
    
    // 打印网络状态（精简版）
    Serial0.print("[MQTT] WiFi IP: ");
    Serial0.println(WiFi.localIP());
    
    Serial0.print("[MQTT] 服务器：");
    Serial0.print(_server);
    Serial0.print(":");
    Serial0.println(_port);
    Serial0.print("[MQTT] 客户端 ID: ");
    Serial0.println(_clientId);
    
    // 配置 MQTT 客户端（只配置一次）
    _mqttClient.setServer(_server, _port);
    _mqttClient.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
        mqttCallback(topic, payload, length);
    });
    
    // 【关键修复】优化MQTT参数，提高稳定性
    _mqttClient.setBufferSize(1024);      // 缓冲区大小
    _mqttClient.setKeepAlive(15);         // 【修复】改为15秒心跳间隔（原60秒太长）
    _mqttClient.setSocketTimeout(10);     // 套接字超时
    
    Serial0.println("[MQTT] 配置完成（KeepAlive: 15s）");
    
    // 【电源缓冲】⭐ 等待2秒让WiFi和电源稳定，防止MQTT连接时BROWNOUT！
    // 原因：MQTT.connect()会触发DNS解析+TCP握手+协议协商，
    //       这些操作会导致功耗瞬间飙升，如果电源没有缓冲时间容易欠压
    Serial0.println("[MQTT] ⏳ 电源缓冲中（2秒）...");
    for (int i = 0; i < 20; i++) {  // 20 * 100ms = 2000ms
        delay(100);
        if (i % 5 == 4) {  // 每500ms打印一次进度
            Serial0.print("[MQTT]   缓冲进度: ");
            Serial0.print((i + 1) * 100);
            Serial0.println("ms");
        }
    }
    Serial0.println("[MQTT] ✅ 电源缓冲完成，开始连接");
    
    // 连接前快速准备
    for (int i = 0; i < 3; i++) {
        _mqttClient.loop();
        delay(50);
    }
    
    // 连接 MQTT Broker（带重试机制）
    bool connected = false;
    int retryCount = 0;
    const int maxRetries = 3;  // 【修复】从5次减少到3次
    
    while (!connected && retryCount < maxRetries) {
        Serial0.print("[MQTT] 尝试连接 第 ");
        Serial0.print(retryCount + 1);
        Serial0.println(" 次...");
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial0.println("[MQTT] 错误：WiFi 已断开");
            return false;
        }
        
        _mqttClient.loop();
        delay(50);
        
        if (_mqttClient.connected()) {
            connected = true;
            break;
        }
        
        // 执行连接
        if (username != nullptr && strlen(username) > 0) {
            connected = _mqttClient.connect(_clientId, _username, _mqttPassword);
        } else {
            connected = _mqttClient.connect(_clientId);
        }
        
        if (connected) {
            Serial0.println("[MQTT] 连接成功！");
            _tcpConnected = true;
            
            // 【关键修复】设置协议类型为MQTT，否则update()会走TCP分支！
            _protocol = PROTOCOL_MQTT;
            
            // 【新增】重置重连状态机
            _isReconnecting = false;
            _reconnectRetryCount = 0;
            _reconnectDelay = 1000;
            
            // 【关键修复】订阅下行命令主题
            if (subTopic != nullptr && strlen(subTopic) > 0) {
                Serial0.print("[MQTT] 订阅主题: ");
                Serial0.println(subTopic);
                if (mqttSubscribe(subTopic, 1)) {
                    Serial0.println("[MQTT] ✅ 订阅成功");
                } else {
                    Serial0.println("[MQTT] ❌ 订阅失败");
                }
            }
            
            triggerStatusCallback(NETWORK_CONNECTED, "MQTT 连接成功");
        } else {
            int state = _mqttClient.state();
            Serial0.print("[MQTT] 连接失败，错误码：");
            Serial0.println(state);
            
            switch (state) {
                case -4:
                    Serial0.println("[MQTT] 错误：连接被拒绝");
                    break;
                case -3:
                    Serial0.println("[MQTT] 错误：连接丢失");
                    break;
                case -2:
                    Serial0.println("[MQTT] 错误：连接失败");
                    break;
                default:
                    Serial0.println("[MQTT] 未知错误");
                    break;
            }
            
            retryCount++;
            if (retryCount < maxRetries) {
                // 【电源恢复】⭐ 等待3秒让电源恢复，防止连续重试导致BROWNOUT
                Serial0.println("[MQTT] ⏳ 等待3秒让电源恢复...");
                for (int i = 0; i < 30; i++) {  // 30 * 100ms = 3000ms
                    delay(100);
                    if (i % 10 == 9) {  // 每1秒打印一次进度
                        Serial0.print("[MQTT]   恢复进度: ");
                        Serial0.print((i + 1) * 100);
                        Serial0.println("ms");
                    }
                }
                Serial0.println("[MQTT] ✅ 电源恢复完成");
            }
        }
    }
    
    return connected;
}

/**
 * @brief 发布 MQTT 消息到指定主题
 * @param topic MQTT 主题
 * @param payload 消息内容
 * @param length 消息长度
 * @param retained 是否保留消息
 * @return true-发布成功，false-失败
 */
bool WiFiNetwork::mqttPublish(const char* topic, const uint8_t* payload, uint16_t length, bool retained) {
    if (!_mqttClient.connected() || topic == nullptr) {
        return false;
    }
    return _mqttClient.publish(topic, payload, length, retained);
}

/**
 * @brief 订阅 MQTT 主题
 * @param topic MQTT 主题
 * @param qos QoS 等级（0, 1, 2）
 * @return true-订阅成功，false-失败
 */
bool WiFiNetwork::mqttSubscribe(const char* topic, uint8_t qos) {
    if (!_mqttClient.connected() || topic == nullptr) {
        return false;
    }
    return _mqttClient.subscribe(topic, qos);
}

/**
 * @brief 检查 MQTT 是否已连接
 * @return true-已连接，false-未连接
 */
bool WiFiNetwork::isMqttConnected() {
    return _mqttClient.connected();
}

/**
 * @brief 获取 Wi-Fi 信号强度
 * @return RSSI 值（dBm），未连接返回 -127
 */
int WiFiNetwork::getRSSI() {
    if (WiFi.status() != WL_CONNECTED) {
        return -127;
    }
    return WiFi.RSSI();
}

/**
 * @brief 获取本地 IP 地址
 * @param buffer 输出缓冲区（至少 16 字节）
 * @param bufferSize 缓冲区大小
 * @return true-获取成功，false-失败
 */
bool WiFiNetwork::getLocalIP(char* buffer, size_t bufferSize) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    IPAddress ip = WiFi.localIP();
    snprintf(buffer, bufferSize, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

/**
 * @brief MQTT 回调函数
 * @param topic 主题
 * @param payload 消息内容
 * @param length 消息长度
 * @details 当收到 MQTT 消息时调用，转发给数据回调
 */
void WiFiNetwork::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    Serial0.print("[MQTT] 收到消息，主题：");
    Serial0.print(topic);
    Serial0.print("，内容：");
    for (unsigned int i = 0; i < length; i++) {
        Serial0.print((char)payload[i]);
    }
    Serial0.println();
    
    if (_dataCallback != nullptr) {
        _dataCallback(payload, length, _userData);
    }
}
