/**
 * @file main.cpp
 * @brief SM-C0x 睡眠呼吸心跳雷达系统主程序
 * @details 本程序基于 ESP32 平台，通过 R60ABD1 60GHz 毫米波雷达模块
 *          采集人体存在、心率、呼吸、睡眠状态等数据，并通过网络
 *          （4G/Wi-Fi/蓝牙）将数据以 JSON 格式发送到中国电信天翼物联平台。
 */

#include <Arduino.h>
#include "RadarR60A.h"
#include "NetworkManager.h"
#include "Protocol.h"
#include "DataSender.h"
#include "CommandHandler.h"
#include "OtaManager.h"
 
/**
 * @brief 雷达模块串口引脚配置
 */
#define RADAR_RX_PIN 16
#define RADAR_TX_PIN 17
#define RADAR_BAUD_RATE 115200 

/**
 * @brief 网络连接模式配置
 */
#define NETWORK_TYPE USE_WIFI

/**
 * @brief 服务器地址和端口配置
 */
#define SERVER_IP "broker.emqx.io"
#define SERVER_PORT 1883

/**
 * @brief MQTT 配置
 */
#define MQTT_CLIENT_ID "SleepMonitor_ESP32S3_001"
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_TOPIC "SMtest"
#define MQTT_SUB_TOPIC "SMtest/cmd"  // 下行命令订阅主题

/**
 * @brief 数据发送间隔（毫秒）
 */
#define SEND_INTERVAL 10000
#define HEARTBEAT_INTERVAL 300000  // 心跳上报间隔 5分钟

/**
 * @brief 4G APN 名称
 */
#define APN_NAME "CMNET"

/**
 * @brief Wi-Fi 连接凭证
 */
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"

/**
 * @brief 设备标识信息
 */
#define DEVICE_IMEI "861810060009388"
#define DEVICE_IMSI "460110416596449"
#define DEVICE_ICCID "89860319967554594540"
#define DEVICE_SW_VERSION "1.0"
#define DEVICE_HD_VERSION "1.0"

/**
 * @brief 设备参数（可通过下发指令修改）
 */
DeviceParams deviceParams = {
    .detectionMode = DETECT_MODE_REAL_TIME,
    .heartRateSwitch = true,
    .breathingSwitch = true,
    .sleepSwitch = true,
    .longTimeNoTimerSwitch = false,
    .unmanneDuration = 60,
    .existSwitch = true,
    .abnormalStruggleSwitch = false
};

/**
 * @brief 全局对象实例
 */
RadarR60A radar(RADAR_RX_PIN, RADAR_TX_PIN, RADAR_BAUD_RATE);
NetworkManager networkManager;
NetworkConfig networkConfig;
Protocol protocol;
DataSender dataSender(protocol, networkManager);
OtaManager otaManager;
CommandHandler commandHandler(protocol, dataSender, radar, &deviceParams, otaManager);

unsigned long lastSendTime = 0;
unsigned long lastHeartbeatTime = 0;
bool networkReady = false;

/**
 * @brief 网络状态变化回调函数
 */
void onNetworkStatusChanged(NetworkStatus status, NetworkType type, const char* message, void* userData) {
    Serial0.print("[网络] ");
    switch (type) {
        case NETWORK_TYPE_4G: Serial0.print("4G"); break;
        case NETWORK_TYPE_BLUETOOTH: Serial0.print("蓝牙"); break;
        case NETWORK_TYPE_WIFI: Serial0.print("Wi-Fi"); break;
    }
    Serial0.print(" - ");
    switch (status) {
        case NETWORK_CONNECTED: Serial0.print("已连接"); break;
        case NETWORK_DISCONNECTED: Serial0.print("已断开"); break;
        case NETWORK_CONNECTING: Serial0.print("连接中"); break;
        case NETWORK_ERROR: Serial0.print("错误"); break;
    }
    Serial0.print(": ");
    Serial0.println(message);
    
    if (type != NETWORK_TYPE_BLUETOOTH) {
        networkReady = (status == NETWORK_CONNECTED);
    }
}

/**
 * @brief 网络数据接收回调函数
 */
void onNetworkDataReceived(const uint8_t* data, uint16_t length, void* userData) {
    commandHandler.handleCommand(data, length);
}

/**
 * @brief 初始化网络配置
 */
void initNetworkConfig() {
    memset(&networkConfig, 0, sizeof(NetworkConfig));
    
    strncpy(networkConfig.ssid, WIFI_SSID, sizeof(networkConfig.ssid) - 1);
    strncpy(networkConfig.password, WIFI_PASSWORD, sizeof(networkConfig.password) - 1);
    strncpy(networkConfig.server, SERVER_IP, sizeof(networkConfig.server) - 1);
    networkConfig.port = SERVER_PORT;
    strncpy(networkConfig.apn, APN_NAME, sizeof(networkConfig.apn) - 1);
    
    networkConfig.useMqtt = true;
    strncpy(networkConfig.mqttClientId, MQTT_CLIENT_ID, sizeof(networkConfig.mqttClientId) - 1);
    strncpy(networkConfig.mqttUsername, MQTT_USERNAME, sizeof(networkConfig.mqttUsername) - 1);
    strncpy(networkConfig.mqttPassword, MQTT_PASSWORD, sizeof(networkConfig.mqttPassword) - 1);
    strncpy(networkConfig.mqttTopic, MQTT_TOPIC, sizeof(networkConfig.mqttTopic) - 1);
    strncpy(networkConfig.mqttSubTopic, MQTT_SUB_TOPIC, sizeof(networkConfig.mqttSubTopic) - 1);
    
    networkConfig.autoReconnect = true;
    networkConfig.reconnectInterval = 10000;
}

/**
 * @brief 定时数据发送任务
 */
void sendPeriodicData() {
    Serial0.println("----------------------------------------");
    Serial0.print("[系统] 准备发送数据，网络：");
    Serial0.println(networkReady ? "已连接" : "未连接");
    
    const RadarR60A::RadarData& radarData = radar.getData();
    
    // 根据设备参数开关控制数据上报
    if (deviceParams.existSwitch) {
        dataSender.sendPresenceInfo(radarData);
    }
        
    // 如果检测到有人，或者有呼吸/心率数据（说明有人），则发送相关数据
    bool isPresent = radarData.isDetected || radarData.breathRate > 0 || radarData.heartRate > 0;
    if (isPresent) {
        if (deviceParams.breathingSwitch || deviceParams.heartRateSwitch) {
            dataSender.sendBreathHeartInfo(radarData);
        }
        if (deviceParams.sleepSwitch) {
            dataSender.sendSleepSummary(radarData);
        }
    }
    
    Serial0.println("----------------------------------------");
}

/**
 * @brief Arduino 初始化函数
 */
void setup() {
    Serial0.begin(115200);
    delay(2000);
    
    Serial0.println("========================================");
    Serial0.println("  SM-C0x 睡眠呼吸心跳雷达系统 v1.0");
    Serial0.println("========================================");
     
    Serial0.println("[System] 初始化协议模块...");
    protocol.setDeviceInfo(DEVICE_IMEI, DEVICE_IMSI, DEVICE_ICCID);
    protocol.setVersionInfo(DEVICE_SW_VERSION, DEVICE_HD_VERSION);
    Serial0.println("[System] 协议模块初始化完成");
    
    Serial0.println("[System] 初始化雷达模块...");
    if (radar.begin()) {
        Serial0.println("[System] 雷达模块初始化完成");
        Serial0.println("[System] 根据设备参数配置雷达检测功能...");
        radar.configureDetection(&deviceParams);
    } else {
        Serial0.println("[System] 雷达模块初始化失败!");
    }
    
    Serial0.println("[System] 初始化网络模块...");
    initNetworkConfig();
    
    if (!networkManager.begin(&networkConfig)) {
        Serial0.println("[System] 网络管理器初始化失败");
    } else {
        Serial0.println("[System] 网络管理器初始化成功");
        networkManager.setStatusCallback(onNetworkStatusChanged);
        networkManager.setDataCallback(onNetworkDataReceived);
        Serial0.println("[System] 开始连接网络...");
        networkManager.connect();
    }
    
    Serial0.println("系统启动完成!");
    Serial0.println("========================================");
    
    delay(5000);
    dataSender.sendDeviceParams(deviceParams);
}

/**
 * @brief Arduino 主循环函数
 */
void loop() {
    radar.update();
    networkManager.update();
    protocol.setSignalStrength(28);
    radar.printData();
    
    if (millis() - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
        lastHeartbeatTime = millis();
        dataSender.sendHeartbeat();
    }
    
    if (millis() - lastSendTime >= SEND_INTERVAL) {
        lastSendTime = millis();
        sendPeriodicData();
    }
    
    delay(1000);
}