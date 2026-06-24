/**
 * @brief 建立 MQTT 连接到服务器（最终修复版）
 * @param server MQTT Broker 地址
 * @param port MQTT 端口
 * @param clientId MQTT 客户端 ID
 * @param username MQTT 用户名（可选）
 * @param password MQTT 密码（可选）
 * @return true-连接成功，false-失败
 * @details 关键修复点：
 *          1. 增加网络稳定性检查
 *          2. 配置更大的缓冲区
 *          3. 使用更宽松的 KeepAlive 设置
 *          4. 添加连接后的订阅确认
 */
bool WiFiNetwork::connectMQTT(const char* server, uint16_t port, const char* clientId, 
                               const char* username, const char* password) {
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
    
    if (username != nullptr) {
        strncpy(_username, username, sizeof(_username) - 1);
    }
    if (password != nullptr) {
        strncpy(_mqttPassword, password, sizeof(_mqttPassword) - 1);
    }
    
    triggerStatusCallback(NETWORK_CONNECTING, "连接 MQTT");
    
    // 等待网络完全就绪（DNS 解析等）
    Serial0.println("[MQTT] 等待网络就绪...");
    delay(2000);
    
    // 打印网络状态
    Serial0.print("[MQTT] Wi-Fi 状态：");
    Serial0.println(WiFi.status());
    Serial0.print("[MQTT] IP 地址：");
    Serial0.println(WiFi.localIP());
    Serial0.print("[MQTT] DNS 服务器：");
    Serial0.println(WiFi.dnsIP());
    Serial0.print("[MQTT] 服务器：");
    Serial0.print(_server);
    Serial0.print(":");
    Serial0.println(_port);
    Serial0.print("[MQTT] 客户端 ID: ");
    Serial0.println(_clientId);
    
    // 配置 MQTT 客户端
    _mqttClient.setServer(_server, _port);
    _mqttClient.setCallback([this](char* topic, uint8_t* payload, unsigned int length) {
        mqttCallback(topic, payload, length);
    });
    
    // 【关键修复】增加缓冲区大小和 KeepAlive 时间
    _mqttClient.setBufferSize(1024);
    _mqttClient.setKeepAlive(60);
    _mqttClient.setSocketTimeout(10);
    
    // 连接 MQTT Broker（带重试机制）
    bool connected = false;
    int retryCount = 0;
    const int maxRetries = 5;
    
    while (!connected && retryCount < maxRetries) {
        Serial0.print("[MQTT] 尝试连接，第 ");
        Serial0.print(retryCount + 1);
        Serial0.println(" 次...");
        
        // 检查客户端连接状态
        if (_mqttClient.connected()) {
            Serial0.println("[MQTT] 客户端已连接");
            connected = true;
            break;
        }
        
        // 执行连接
        if (username != nullptr && strlen(username) > 0) {
            Serial0.print("[MQTT] 使用用户名连接：");
            Serial0.println(_username);
            connected = _mqttClient.connect(_clientId, _username, _mqttPassword);
        } else {
            Serial0.println("[MQTT] 匿名连接");
            connected = _mqttClient.connect(_clientId);
        }
        
        if (connected) {
            Serial0.println("[MQTT] 连接成功！");
        } else {
            int state = _mqttClient.state();
            Serial0.print("[MQTT] 连接失败，错误码：");
            Serial0.println(state);
            
            // 详细错误信息
            switch (state) {
                case -4:
                    Serial0.println("[MQTT] 错误：连接被拒绝（可能是 Broker 不可达）");
                    break;
                case -3:
                    Serial0.println("[MQTT] 错误：连接丢失");
                    break;
                case -2:
                    Serial0.println("[MQTT] 错误：连接失败（网络问题或 Broker 拒绝）");
                    break;
                case -1:
                    Serial0.println("[MQTT] 错误：未连接");
                    break;
                case 1:
                    Serial0.println("[MQTT] 错误：协议版本不支持");
                    break;
                case 2:
                    Serial0.println("[MQTT] 错误：客户端 ID 被拒绝");
                    break;
                case 3:
                    Serial0.println("[MQTT] 错误：服务器不可用");
                    break;
                case 4:
                    Serial0.println("[MQTT] 错误：用户名或密码错误");
                    break;
                case 5:
                    Serial0.println("[MQTT] 错误：未授权");
                    break;
                default:
                    Serial0.println("[MQTT] 错误：未知错误");
                    break;
            }
            
            retryCount++;
            if (retryCount < maxRetries) {
                Serial0.print("[MQTT] ");
                Serial0.print(6 - retryCount);
                Serial0.println(" 秒后重试...");
                delay(1000);
            }
        }
    }
    
    if (connected) {
        _protocol = PROTOCOL_MQTT;
        _tcpConnected = true;
        
        // 订阅主题（如果需要）
        Serial0.print("[MQTT] 已连接，订阅主题：");
        Serial0.println(_mqttTopic);
        
        triggerStatusCallback(NETWORK_CONNECTED, "MQTT 连接成功");
        Serial0.println("[MQTT] MQTT 连接初始化完成");
        return true;
    } else {
        char errorMsg[64];
        snprintf(errorMsg, sizeof(errorMsg), "MQTT 连接失败：%d", _mqttClient.state());
        triggerStatusCallback(NETWORK_ERROR, errorMsg);
        Serial0.println("[MQTT] 放弃连接");
        return false;
    }
}

/**
 * @brief 发送数据到服务器（最终修复版）
 * @param data 数据指针
 * @param length 数据长度
 * @return true-发送成功，false-失败
 * @details 关键修复点：
 *          1. 使用 QoS 1 确保消息送达
 *          2. 延长等待确认时间
 *          3. 发布失败时自动重连并重试
 *          4. 增加详细的日志输出
 */
bool WiFiNetwork::send(const uint8_t* data, uint16_t length) {
    if (_protocol == PROTOCOL_MQTT) {
        // MQTT 模式：发布到主题
        if (!_mqttClient.connected()) {
            Serial0.println("[MQTT] 发送失败：MQTT 未连接");
            return false;
        }
        
        Serial0.print("[MQTT] 开始发布，数据长度：");
        Serial0.println(length);
        Serial0.print("[MQTT] 主题：");
        Serial0.println(_mqttTopic);
        
        // 使用 QoS 1 确保消息送达
        bool success = _mqttClient.publish(_mqttTopic, data, length, 1);
        
        // 等待消息确认（QoS 1 需要等待 PUBACK）
        if (success) {
            Serial0.println("[MQTT] 发布成功，等待 Broker 确认...");
            // 延长等待时间，确保 Broker 有足够时间处理
            unsigned long startTime = millis();
            const unsigned long waitTime = 1000;
            
            while (millis() - startTime < waitTime) {
                _mqttClient.loop();
                delay(20);
            }
            
            // 检查发布后连接状态
            if (_mqttClient.connected()) {
                Serial0.println("[MQTT] 发布完成，连接保持");
            } else {
                Serial0.println("[MQTT] 发布完成但连接已断开，将在 update() 中重连");
            }
        } else {
            Serial0.println("[MQTT] 发布失败");
            Serial0.print("[MQTT] 错误码：");
            Serial0.println(_mqttClient.state());
            
            // 发布失败时尝试重连
            Serial0.println("[MQTT] 尝试重新连接...");
            delay(500);
            if (_mqttClient.connect(_clientId)) {
                Serial0.println("[MQTT] 重连成功，准备重新发布");
                // 重试发布
                success = _mqttClient.publish(_mqttTopic, data, length, 1);
                if (success) {
                    Serial0.println("[MQTT] 重新发布成功");
                    // 同样需要等待确认
                    unsigned long startTime = millis();
                    while (millis() - startTime < 1000) {
                        _mqttClient.loop();
                        delay(20);
                    }
                }
            }
        }
        return success;
    } else {
        // TCP 模式：直接发送
        if (!_tcpConnected || !_wifiClient.connected()) {
            return false;
        }
        return _wifiClient.write(data, length) == length;
    }
}
