/**
 * @file NetworkManager.cpp
 * @brief 网络连接管理器实现
 * @details 实现主备网络模式：
 *          - 主网络：Wi-Fi（日常数据发送）
 *          - 备用网络：4G（Wi-Fi 断开时自动切换）
 *          - 蓝牙：用于配置 Wi-Fi 名称和密码
 */

#include "NetworkManager.h"

/**
 * @brief 构造函数
 * @details 初始化所有成员变量为空/默认值
 */
NetworkManager::NetworkManager() {
    _module4G = nullptr;
    _bluetooth = nullptr;
    _wifi = nullptr;
    _activeNetwork = NETWORK_TYPE_NONE;
    _configPhase = CONFIG_PHASE_WAITING;
    _initialized = false;
    _bluetoothInitialized = false;  // 【新增】蓝牙初始化标志
    _lastUpdateTime = 0;
    _lastSwitchTime = 0;
    _wifiFailTime = 0;
    _dataCallback = nullptr;
    _statusCallback = nullptr;
    memset(&_config, 0, sizeof(_config));
}

/**
 * @brief 析构函数
 * @details 断开所有网络连接，释放所有网络模块内存
 */
NetworkManager::~NetworkManager() {
    disconnect();
    if (_module4G != nullptr) delete _module4G;
    if (_bluetooth != nullptr) delete _bluetooth;
    if (_wifi != nullptr) delete _wifi;
}

/**
 * @brief 初始化网络管理器
 * @param config 网络配置指针（可为 nullptr，使用默认配置）
 * @return true-初始化成功，false-失败
 * @details 创建 4G、蓝牙、Wi-Fi 三个模块实例，并为每个模块
 *          注册状态回调和数据回调
 */
bool NetworkManager::begin(NetworkConfig* config) {
    if (config != nullptr) {
        setConfig(config);
    }
    
    if (_module4G == nullptr) {
        _module4G = new A7670C(4, 5, 18, 115200);
        if (_module4G != nullptr) {
            _module4G->setStatusCallback(staticStatusCallback, this);
            _module4G->setDataCallback(staticDataCallback, this);
        }
    }
    
    if (_bluetooth == nullptr) {
        _bluetooth = new BluetoothNetwork("SleepMonitor_BT");
        if (_bluetooth != nullptr) {
            _bluetooth->setStatusCallback(staticStatusCallback, this);
            _bluetooth->setDataCallback(staticDataCallback, this);
        }
    }
    
    if (_wifi == nullptr) {
        _wifi = new WiFiNetwork();
        if (_wifi != nullptr) {
            _wifi->setStatusCallback(staticStatusCallback, this);
            _wifi->setDataCallback(staticDataCallback, this);
        }
    }
    
    // 【修复】⭐ Flash配置优先级最高！
    // 原因：之前逻辑是"只有内存中没有配置才加载Flash"，
    //       但main.cpp中预定义了WIFI_SSID="YourWiFiSSID"占位符，
    //       导致strlen(_config.ssid)!=0，跳过了Flash加载！
    //
    // 正确逻辑（优先级从高到低）：
    //   1. Flash保存的配置（用户通过蓝牙配置过的，最可靠）
    //   2. 代码预定义的配置（可能只是占位符）
    //   3. 都没有 → 进入蓝牙等待模式
    Serial0.println("[系统] 检查是否有已保存的WiFi配置...");
    bool hasFlashConfig = loadWifiConfig();
    
    if (hasFlashConfig) {
        Serial0.println("[系统] ✅ 使用Flash中的WiFi配置（优先级最高）");
    } else {
        // Flash无配置，检查内存中是否有有效配置
        if (strlen(_config.ssid) > 0 && strcmp(_config.ssid, "YourWiFiSSID") != 0) {
            // 内存中有非占位符的有效配置
            Serial0.println("[系统] 使用代码预定义的WiFi配置");
        } else {
            // 无任何有效配置
            Serial0.println("[系统] ⚠️ 无有效WiFi配置，将通过蓝牙获取");
            memset(_config.ssid, 0, sizeof(_config.ssid));
            memset(_config.password, 0, sizeof(_config.password));
        }
    }
    
    _initialized = true;
    return true;
}

/**
 * @brief 连接主网络（Wi-Fi）和备用网络（4G）
 * @return true-主网络连接成功，false-主网络失败但备用网络可能成功
 * @details 先打开蓝牙等待 Wi-Fi 配置，收到配置后连接 Wi-Fi，
 *          若失败则自动连接 4G 作为备用网络
 */
bool NetworkManager::connect() {
    if (!_initialized) return false;
    
    // 【关键修复】⭐ 始终确保蓝牙已初始化并运行！
    // 原因：蓝牙是接收WiFi配置的唯一入口，必须始终保持可用。
    //       无论当前处于哪个阶段（等待/连接/已连接），
    //       用户都可能通过蓝牙发送新的WiFi配置。
    if (!_bluetoothInitialized) {
        Serial0.println("[网络] 初始化蓝牙模块...");
        if (initBluetooth()) {
            _bluetoothInitialized = true;
            Serial0.println("[网络] ✅ 蓝牙已启动，可随时接收新WiFi配置");
        } else {
            Serial0.println("[网络] ⚠️ 蓝牙初始化失败，将在下次重试");
        }
    }
    
    // 等待 Wi-Fi 配置阶段：提示用户并通过蓝牙接收配置
    if (_configPhase == CONFIG_PHASE_WAITING) {
        Serial0.println("[网络] ⏳ 等待 WiFi 配置（通过蓝牙）...");
        Serial0.println("[网络] 配置格式: SET_WIFI:SSID:PASSWORD");
        return false; // 继续等待蓝牙接收配置
    }
    
    // 已收到 Wi-Fi 配置（来自Flash或蓝牙），尝试连接 Wi-Fi
    if (_configPhase == CONFIG_PHASE_RECEIVED) {
        Serial0.println("[网络] 已收到 Wi-Fi 配置，开始连接 Wi-Fi...");
        _configPhase = CONFIG_PHASE_CONNECTING;
        
        if (initWiFi()) {
            _activeNetwork = NETWORK_TYPE_WIFI;
            _lastSwitchTime = millis();
            Serial0.println("[网络] Wi-Fi 连接成功，使用 Wi-Fi 作为主网络");
            return true;
        }
        
        Serial0.println("[网络] Wi-Fi 连接失败，尝试连接备用网络 4G...");
        _wifiFailTime = millis();
    }
    
    // Wi-Fi 失败，尝试 4G
    if (init4G()) {
        _activeNetwork = NETWORK_TYPE_4G;
        _lastSwitchTime = millis();
        Serial0.println("[网络] 4G 连接成功，使用 4G 作为备用网络");
        return true;
    }
    
    Serial0.println("[网络] 所有网络连接失败");
    return false;
}

/**
 * @brief 处理蓝牙接收到的 Wi-Fi 配置数据
 * @param data 数据指针
 * @param length 数据长度
 * @details 解析格式：SET_WIFI:SSID:PASSWORD
 */
void NetworkManager::handleWifiConfig(const uint8_t* data, uint16_t length) {
    if (data == nullptr || length == 0) return;
    
    // 复制数据到临时缓冲区
    char buffer[256];
    if (length >= sizeof(buffer)) length = sizeof(buffer) - 1;
    memcpy(buffer, data, length);
    buffer[length] = '\0';
    
    Serial0.print("[网络] 收到蓝牙数据: ");
    Serial0.println(buffer);
    
    // 解析 SET_WIFI:SSID:PASSWORD 格式
    if (strncmp(buffer, "SET_WIFI:", 9) == 0) {
        char* ssidStart = buffer + 9;
        char* passwordPos = strchr(ssidStart, ':');
        
        if (passwordPos != nullptr) {
            *passwordPos = '\0';
            char* password = passwordPos + 1;
            
            // 保存 Wi-Fi 配置
            strncpy(_config.ssid, ssidStart, sizeof(_config.ssid) - 1);
            strncpy(_config.password, password, sizeof(_config.password) - 1);
            
            Serial0.print("[网络] Wi-Fi 配置已保存 - SSID: ");
            Serial0.print(_config.ssid);
            Serial0.print(", 密码长度: ");
            Serial0.println(strlen(_config.password));
            
            // 【关键修复】⭐ 收到新配置后强制触发WiFi重连流程！
            // 原因：用户可能在使用4G或旧WiFi连接时发送新配置，
            //       需要立即断开现有连接并用新配置重连WiFi。
            //       设置为CONFIG_PHASE_RECEIVED会让下一次update()调用connect()，
            //       connect()会完整执行：断开旧连接 → 连接新WiFi → 建立MQTT
            _configPhase = CONFIG_PHASE_RECEIVED;
            
            // 如果当前正在使用网络，先断开（让connect()能重新初始化）
            if (_activeNetwork != NETWORK_TYPE_NONE) {
                Serial0.println("[网络] 检测到新WiFi配置，准备切换到新网络...");
                
                // 断开当前活跃的网络
                if (_activeNetwork == NETWORK_TYPE_WIFI && _wifi != nullptr) {
                    _wifi->disconnect();
                    Serial0.println("[网络] 已断开当前WiFi连接");
                } else if (_activeNetwork == NETWORK_TYPE_4G && _module4G != nullptr) {
                    _module4G->disconnect();
                    Serial0.println("[网络] 已断开当前4G连接");
                }
                
                _activeNetwork = NETWORK_TYPE_NONE;
                _lastSwitchTime = millis();  // 重置切换时间
            }
            
            Serial0.println("[网络] ✅ 新配置已就绪，将在下次循环中连接WiFi");
            
            // 【新增】保存到Flash，重启后自动使用
            saveWifiConfig();
        } else {
            Serial0.println("[网络] Wi-Fi 配置格式错误，缺少密码");
        }
    }
}

/**
 * @brief 检查是否已收到 Wi-Fi 配置
 * @return true-已收到配置，false-未收到
 */
bool NetworkManager::hasWifiConfig() {
    return strlen(_config.ssid) > 0 && _configPhase >= CONFIG_PHASE_RECEIVED;
}

/**
 * @brief 周期性更新函数
 * @details 在主循环中调用，执行以下操作：
 *          1. 等待蓝牙配置时，更新蓝牙模块状态
 *          2. 已连接网络时，更新当前活跃网络的状态
 *          3. 检查主网络健康度，自动切换网络
 *          4. 收到 Wi-Fi 配置后自动连接网络
 */
void NetworkManager::update() {
    if (!_initialized) return;
    
    unsigned long now = millis();
    if (now - _lastUpdateTime < 100) return;
    _lastUpdateTime = now;
    
    // 等待蓝牙配置阶段
    if (_configPhase == CONFIG_PHASE_WAITING && _bluetooth != nullptr) {
        _bluetooth->update();
        return;
    }
    
    // 已收到配置，尝试连接网络
    if (_configPhase == CONFIG_PHASE_RECEIVED) {
        connect();
        return;
    }
    
    // 正常网络运行阶段
    if (_activeNetwork == NETWORK_TYPE_WIFI && _wifi != nullptr) {
        _wifi->update();
    } else if (_activeNetwork == NETWORK_TYPE_4G && _module4G != nullptr) {
        _module4G->update();
    }
    
    // 蓝牙保持运行，用于后续配置
    if (_bluetooth != nullptr) {
        _bluetooth->update();
    }
    
    checkAutoSwitch();
}

/**
 * @brief 断开所有网络连接
 * @details 依次断开 Wi-Fi、4G、蓝牙连接，重置活跃网络状态
 */
void NetworkManager::disconnect() {
    if (_wifi != nullptr) {
        _wifi->disconnect();
    }
    if (_module4G != nullptr) {
        _module4G->disconnect();
    }
    if (_bluetooth != nullptr) {
        _bluetooth->disconnect();
    }
    _activeNetwork = NETWORK_TYPE_NONE;
    _lastSwitchTime = millis();
}

/**
 * @brief 发送数据到服务器
 * @param data 数据指针
 * @param length 数据长度
 * @return true-发送成功，false-失败
 * @details 优先使用当前活跃网络发送数据。
 *          若当前网络未连接，尝试切换到备用网络
 */
bool NetworkManager::send(const uint8_t* data, uint16_t length) {
    if (!_initialized) return false;
    
    if (_activeNetwork == NETWORK_TYPE_WIFI && _wifi != nullptr) {
        if (_wifi->getStatus() == NETWORK_CONNECTED) {
            return _wifi->send(data, length);
        }
    } else if (_activeNetwork == NETWORK_TYPE_4G && _module4G != nullptr) {
        if (_module4G->getStatus() == NETWORK_CONNECTED) {
            return _module4G->send(data, length);
        }
    }
    
    return false;
}

/**
 * @brief 获取当前使用的网络类型
 * @return NetworkType 枚举值（NETWORK_TYPE_WIFI 或 NETWORK_TYPE_4G）
 */
NetworkType NetworkManager::getCurrentNetworkType() {
    return _activeNetwork;
}

/**
 * @brief 获取当前网络状态
 * @return NetworkStatus 枚举值
 */
NetworkStatus NetworkManager::getNetworkStatus() {
    if (_activeNetwork == NETWORK_TYPE_WIFI && _wifi != nullptr) {
        return _wifi->getStatus();
    } else if (_activeNetwork == NETWORK_TYPE_4G && _module4G != nullptr) {
        return _module4G->getStatus();
    }
    return NETWORK_DISCONNECTED;
}

/**
 * @brief 检查是否有网络连接（Wi-Fi 或 4G）
 * @return true-至少有一个网络已连接，false-都未连接
 */
bool NetworkManager::isConnected() {
    return isWifiConnected() || is4GConnected();
}

/**
 * @brief 检查 Wi-Fi 是否已连接
 * @return true-已连接，false-未连接
 */
bool NetworkManager::isWifiConnected() {
    if (_wifi == nullptr) return false;
    return _wifi->getStatus() == NETWORK_CONNECTED;
}

/**
 * @brief 检查 4G 是否已连接
 * @return true-已连接，false-未连接
 */
bool NetworkManager::is4GConnected() {
    if (_module4G == nullptr) return false;
    return _module4G->getStatus() == NETWORK_CONNECTED;
}

/**
 * @brief 设置网络配置参数
 * @param config 配置结构体指针
 * @return true-设置成功，false-参数为空
 */
bool NetworkManager::setConfig(NetworkConfig* config) {
    if (config == nullptr) return false;
    memcpy(&_config, config, sizeof(NetworkConfig));
    return true;
}

/**
 * @brief 获取 4G 模块指针
 * @return A7670C 指针（可能为 nullptr）
 */
A7670C* NetworkManager::get4GModule() {
    return _module4G;
}

/**
 * @brief 获取蓝牙模块指针
 * @return BluetoothNetwork 指针（可能为 nullptr）
 */
BluetoothNetwork* NetworkManager::getBluetoothModule() {
    return _bluetooth;
}

/**
 * @brief 获取 Wi-Fi 模块指针
 * @return WiFiNetwork 指针（可能为 nullptr）
 */
WiFiNetwork* NetworkManager::getWiFiModule() {
    return _wifi;
}

/**
 * @brief 设置数据接收回调函数
 * @param callback 回调函数指针
 */
void NetworkManager::setDataCallback(NetworkDataCallback callback) {
    _dataCallback = callback;
}

/**
 * @brief 设置网络状态变化回调函数
 * @param callback 回调函数指针
 */
void NetworkManager::setStatusCallback(NetworkStatusCallback callback) {
    _statusCallback = callback;
}

/**
 * @brief 初始化 Wi-Fi 模块并连接服务器
 * @return true-成功，false-失败
 * @details 调用 WiFiNetwork::begin() 启动模块，
 *          连接 Wi-Fi 热点，根据配置选择 TCP 或 MQTT 协议连接服务器
 */
bool NetworkManager::initWiFi() {
    if (_wifi == nullptr) return false;
    if (!_wifi->begin()) return false;
    
    if (strlen(_config.ssid) > 0) {
        if (!_wifi->connectToAP(_config.ssid, _config.password)) {
            return false;
        }
        
        if (strlen(_config.server) > 0 && _config.port > 0) {
            // 根据配置选择协议
            if (_config.useMqtt) {
                Serial0.println("[网络] 使用 MQTT 协议连接");
                return _wifi->connectMQTT(_config.server, _config.port, 
                                         _config.mqttClientId,
                                         _config.mqttUsername,
                                         _config.mqttPassword,
                                         _config.mqttTopic,
                                         _config.mqttSubTopic);
            } else {
                Serial0.println("[网络] 使用 TCP 协议连接");
                return _wifi->connectTCP(_config.server, _config.port);
            }
        }
    }
    return true;
}

/**
 * @brief 初始化 4G 模块并连接 TCP 服务器
 * @return true-成功，false-失败
 * @details 调用 A7670C::begin() 启动模块，设置 APN，
 *          若配置了服务器地址和端口则发起 TCP 连接
 */
bool NetworkManager::init4G() {
    if (_module4G == nullptr) return false;
    if (!_module4G->begin()) return false;
    if (strlen(_config.apn) > 0) {
        _module4G->setAPN(_config.apn);
    }
    if (strlen(_config.server) > 0 && _config.port > 0) {
        return _module4G->connectTCP(_config.server, _config.port);
    }
    return true;
}

/**
 * @brief 初始化蓝牙模块
 * @return true-成功，false-失败
 * @details 调用 BluetoothNetwork::begin() 启动 BLE 蓝牙
 */
bool NetworkManager::initBluetooth() {
    if (_bluetooth == nullptr) return false;
    return _bluetooth->begin();
}

/**
 * @brief 切换到指定网络
 * @param networkType 目标网络类型
 * @return true-切换成功，false-失败
 * @details 检查切换冷却时间，断开当前网络后连接新网络
 */
bool NetworkManager::switchToNetwork(NetworkType networkType) {
    if (networkType == _activeNetwork) return isConnected();
    
    if (millis() - _lastSwitchTime < SWITCH_COOLDOWN) {
        return false;
    }
    
    Serial0.print("[网络] 切换到网络: ");
    Serial0.println(networkType == NETWORK_TYPE_WIFI ? "Wi-Fi" : "4G");
    
    disconnect();
    delay(500);
    
    bool success = false;
    if (networkType == NETWORK_TYPE_WIFI) {
        success = initWiFi();
    } else if (networkType == NETWORK_TYPE_4G) {
        success = init4G();
    }
    
    if (success) {
        _activeNetwork = networkType;
        _lastSwitchTime = millis();
        Serial0.println("[网络] 网络切换成功");
    } else {
        Serial0.println("[网络] 网络切换失败");
    }
    
    return success;
}

/**
 * @brief 检查并自动切换网络
 * @details 自动网络切换逻辑：
 *          1. 若当前使用 Wi-Fi 且 Wi-Fi 断开：
 *             - 记录失败时间
 *             - 若超过冷却时间，尝试切换到 4G
 *          2. 若当前使用 4G 且 Wi-Fi 已恢复：
 *             - 若超过冷却时间，切换回 Wi-Fi（主网络优先）
 *          3. 若当前无网络且 Wi-Fi 失败超过重试间隔：
 *             - 尝试重新连接 Wi-Fi
 */
void NetworkManager::checkAutoSwitch() {
    unsigned long now = millis();
    
    if (_activeNetwork == NETWORK_TYPE_WIFI) {
        if (!isWifiConnected()) {
            if (_wifiFailTime == 0) {
                _wifiFailTime = now;
                Serial0.println("[网络] Wi-Fi 连接断开，记录失败时间");
            }
            
            // 【优化】⭐ WiFi断开后先尝试快速重连（5秒内），不要立即切4G
            // 原因：WiFi信号波动是暂时的，大多数情况下几秒内就能恢复，
            //       立即切换到4G会导致不必要的网络切换开销
            unsigned long failDuration = now - _wifiFailTime;
            
            if (failDuration < 5000) {
                // 前5秒：尝试快速重连WiFi
                static unsigned long lastQuickReconnect = 0;
                if (now - lastQuickReconnect >= 2000) {  // 每2秒尝试一次
                    lastQuickReconnect = now;
                    Serial0.println("[网络] 尝试快速重连 WiFi...");
                    switchToNetwork(NETWORK_TYPE_WIFI);
                }
            } else if (now - _lastSwitchTime > SWITCH_COOLDOWN) {
                // 超过5秒仍失败：考虑切换到4G备用网络
                Serial0.print("[网络] WiFi断开 ");
                Serial.print(failDuration / 1000);
                Serial.println("秒，尝试切换到 4G 备用网络");
                switchToNetwork(NETWORK_TYPE_4G);
            }
        } else {
            _wifiFailTime = 0;  // WiFi恢复，清除失败时间
        }
    } else if (_activeNetwork == NETWORK_TYPE_4G) {
        // 【新增】⭐ 4G模式下定期检查WiFi是否恢复可用
        // 原因：用户希望在4G备用网络运行时，也能自动检测WiFi信号恢复，
        //       一旦WiFi可用就切换回主网络（WiFi优先原则）
        static unsigned long lastWifiScanTime = 0;
        if (now - lastWifiScanTime >= 10000) {  // 每10秒检查一次
            lastWifiScanTime = now;
            
            if (isWifiConnected()) {
                // WiFi已连接：立即切回WiFi主网络
                if (now - _lastSwitchTime > SWITCH_COOLDOWN) {
                    Serial0.println("[网络] WiFi已恢复，从4G切换回WiFi主网络");
                    switchToNetwork(NETWORK_TYPE_WIFI);
                }
            } else {
                // WiFi未连接但保存了配置：尝试重连WiFi
                if (strlen(_config.ssid) > 0 && now - _lastSwitchTime > SWITCH_COOLDOWN) {
                    static unsigned long lastWifiReconnectAttempt = 0;
                    // 每30秒尝试一次WiFi重连（避免频繁尝试）
                    if (now - lastWifiReconnectAttempt >= 30000) {
                        lastWifiReconnectAttempt = now;
                        Serial0.println("[网络] 4G模式下检测到WiFi配置，尝试重连WiFi...");
                        switchToNetwork(NETWORK_TYPE_WIFI);
                    }
                }
            }
        }
        
        // 4G断开检测
        if (!is4GConnected() && now - _lastSwitchTime > SWITCH_COOLDOWN) {
            Serial0.println("[网络] 4G 连接断开，尝试重新连接");
            switchToNetwork(NETWORK_TYPE_4G);
        }
    } else {
        if (_wifiFailTime == 0 || now - _wifiFailTime > WIFI_RETRY_INTERVAL) {
            Serial0.println("[网络] 无网络连接，尝试连接 Wi-Fi");
            switchToNetwork(NETWORK_TYPE_WIFI);
        }
    }
}

/**
 * @brief 内部状态变化处理函数
 * @param status 网络状态
 * @param type 网络类型
 * @param message 状态信息
 */
void NetworkManager::onNetworkStatusChanged(NetworkStatus status, NetworkType type, const char* message) {
    Serial0.print("[网络状态] ");
    Serial0.print(type == NETWORK_TYPE_WIFI ? "Wi-Fi" : (type == NETWORK_TYPE_4G ? "4G" : "蓝牙"));
    Serial0.print(": ");
    Serial0.println(message);
    
    if (_statusCallback != nullptr) {
        _statusCallback(status, type, message, nullptr);
    }
}

/**
 * @brief 内部数据接收处理函数
 * @param data 数据指针
 * @param length 数据长度
 */
void NetworkManager::onNetworkDataReceived(const uint8_t* data, uint16_t length) {
    Serial0.print("[网络数据] 收到 ");
    Serial0.print(length);
    Serial0.println(" 字节");
    
    // 【关键修复】⭐ 始终检查Wi-Fi配置命令，不限于等待阶段！
    // 原因：用户可能在使用4G网络或WiFi断开后，通过蓝牙重新发送WiFi配置，
    //       希望设备能够立即使用新配置重连WiFi。
    //       之前bug：只有_configPhase == CONFIG_PHASE_WAITING时才处理，
    //               一旦进入其他阶段（如已连接4G），新配置会被忽略！
    if (length > 9 && strncmp((const char*)data, "SET_WIFI:", 9) == 0) {
        Serial0.println("[网络] 检测到 Wi-Fi 配置命令");
        handleWifiConfig(data, length);
        return;
    }
    
    if (_dataCallback != nullptr) {
        _dataCallback(data, length, nullptr);
    }
}

/**
 * @brief 静态回调转发函数（状态变化）
 * @param status 网络状态
 * @param type 网络类型
 * @param message 状态信息
 * @param userData 用户数据指针（指向 NetworkManager 实例）
 */
void NetworkManager::staticStatusCallback(NetworkStatus status, NetworkType type, const char* message, void* userData) {
    if (userData != nullptr) {
        NetworkManager* manager = static_cast<NetworkManager*>(userData);
        manager->onNetworkStatusChanged(status, type, message);
    }
}

/**
 * @brief 静态回调转发函数（数据接收）
 * @param data 数据指针
 * @param length 数据长度
 * @param userData 用户数据指针（指向 NetworkManager 实例）
 */
void NetworkManager::staticDataCallback(const uint8_t* data, uint16_t length, void* userData) {
    if (userData != nullptr) {
        NetworkManager* manager = static_cast<NetworkManager*>(userData);
        manager->onNetworkDataReceived(data, length);
    }
}

// ==================== WiFi配置持久化存储实现 ====================

/**
 * @brief 保存Wi-Fi配置到Flash持久化存储
 * @return true-保存成功，false-失败
 * @details 使用ESP32 Preferences库将SSID和密码保存到Flash，
 *          重启后可自动加载，无需每次通过蓝牙重新配置。
 *          存储命名空间: "wifi_config"
 *          存储键值:
 *            - "ssid": WiFi名称
 *            - "pass": WiFi密码
 */
bool NetworkManager::saveWifiConfig() {
    if (strlen(_config.ssid) == 0) {
        Serial0.println("[存储] 错误：SSID为空，不保存");
        return false;
    }
    
    Preferences prefs;
    if (!prefs.begin("wifi_config", false)) {  // false = read/write mode
        Serial0.println("[存储] ❌ 无法打开Preferences");
        return false;
    }
    
    // 保存SSID和密码
    prefs.putString("ssid", _config.ssid);
    prefs.putString("pass", _config.password);
    
    // 标记已保存有效配置
    prefs.putBool("valid", true);
    
    prefs.end();
    
    Serial0.println("[存储] ✅ WiFi配置已保存到Flash");
    Serial0.print("[存储]   SSID: ");
    Serial0.println(_config.ssid);
    Serial0.print("[存储]   密码长度: ");
    Serial0.println(strlen(_config.password));
    
    return true;
}

/**
 * @brief 从Flash加载已保存的Wi-Fi配置
 * @return true-加载成功且有有效配置，false-无保存的配置
 * @details 从Preferences中读取之前保存的WiFi账号密码，
 *          如果存在则自动填充到_config并设置CONFIG_PHASE_RECEIVED，
 *          让系统自动进入连接流程
 */
bool NetworkManager::loadWifiConfig() {
    Preferences prefs;
    if (!prefs.begin("wifi_config", true)) {  // true = read-only mode
        Serial0.println("[存储] ⚠️ 无已保存的WiFi配置");
        return false;
    }
    
    // 检查是否有有效配置
    if (!prefs.getBool("valid", false)) {
        Serial0.println("[存储] ⚠️ WiFi配置无效或未保存");
        prefs.end();
        return false;
    }
    
    // 读取SSID和密码
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    
    prefs.end();
    
    // 验证数据有效性
    if (ssid.length() == 0) {
        Serial0.println("[存储] ⚠️ 保存的SSID为空");
        return false;
    }
    
    // 填充到配置结构体
    strncpy(_config.ssid, ssid.c_str(), sizeof(_config.ssid) - 1);
    strncpy(_config.password, pass.c_str(), sizeof(_config.password) - 1);
    
    // 设置配置阶段为已接收，让connect()能直接使用
    _configPhase = CONFIG_PHASE_RECEIVED;
    
    Serial0.println("[存储] ✅ 从Flash加载WiFi配置成功");
    Serial0.print("[存储]   SSID: ");
    Serial0.println(_config.ssid);
    Serial0.print("[存储]   密码长度: ");
    Serial0.println(strlen(_config.password));
    Serial0.println("[存储]   将使用此配置自动连接WiFi...");
    
    return true;
}

/**
 * @brief 清除已保存的Wi-Fi配置
 * @return true-清除成功，false-失败
 * @details 用于用户需要重新配置WiFi时清除旧配置
 */
bool NetworkManager::clearWifiConfig() {
    Preferences prefs;
    if (!prefs.begin("wifi_config", false)) {
        Serial0.println("[存储] ❌ 无法打开Preferences进行清除");
        return false;
    }
    
    prefs.clear();  // 清除整个命名空间
    prefs.end();
    
    // 同时清空内存中的配置
    memset(_config.ssid, 0, sizeof(_config.ssid));
    memset(_config.password, 0, sizeof(_config.password));
    _configPhase = CONFIG_PHASE_WAITING;
    
    Serial0.println("[存储] ✅ 已清除WiFi配置（内存+Flash）");
    
    return true;
}
