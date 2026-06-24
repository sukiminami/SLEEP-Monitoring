/**
 * @file A7670C.h
 * @brief A7670C 4G LTE 模块驱动头文件
 * @details 通过 AT 命令控制合宙 A7670C 4G 模块，实现网络注册、
 *          TCP 连接、数据收发等功能。继承自 NetworkInterface。
 */

#ifndef A7670C_H
#define A7670C_H

#include "NetworkInterface.h"
#include <HardwareSerial.h>

/**
 * @brief 网络注册状态枚举
 */
typedef enum {
    REG_NOT_REGISTERED = 0,    /**< 未注册 */
    REG_REGISTERED_HOME = 1,   /**< 已注册（本地网络） */
    REG_SEARCHING = 2,         /**< 搜索中 */
    REG_REGISTERED_ROAMING = 5 /**< 已注册（漫游） */
} NetworkRegistrationStatus;

/**
 * @brief A7670C 4G 模块驱动类
 * @details 通过 HardwareSerial 与 A7670C 模块通信，使用 AT 命令
 *          控制网络注册、TCP 连接、数据发送等功能。
 */
class A7670C : public NetworkInterface {
public:
    /**
     * @brief 构造函数
     * @param rxPin ESP32 接收引脚（默认 16）
     * @param txPin ESP32 发送引脚（默认 17）
     * @param pwrKeyPin 电源控制引脚（默认 18）
     * @param baudRate 串口波特率（默认 115200）
     */
    A7670C(int rxPin = 16, int txPin = 17, int pwrKeyPin = 18, long baudRate = 115200);
    
    /**
     * @brief 析构函数
     * @details 断开 TCP 连接，释放串口对象
     */
    ~A7670C();
    
    /**
     * @brief 初始化 4G 模块
     * @return true-成功，false-失败
     * @details 创建串口对象，拉高 PWR_KEY 引脚开机，发送 AT 命令确认通信
     */
    bool begin() override;
    
    /**
     * @brief 断开 TCP 连接
     */
    void disconnect() override;
    
    /**
     * @brief 获取网络状态
     * @return NetworkStatus 枚举值
     */
    NetworkStatus getStatus() override;
    
    /**
     * @brief 获取网络类型
     * @return NETWORK_TYPE_4G
     */
    NetworkType getType() override { return NETWORK_TYPE_4G; }
    
    /**
     * @brief 发送数据到 TCP 服务器
     * @param data 数据指针
     * @param length 数据长度
     * @return true-发送成功，false-失败
     * @details 使用 AT+CIPSEND 命令发送数据
     */
    bool send(const uint8_t* data, uint16_t length) override;
    
    /**
     * @brief 周期性更新
     * @details 读取串口缓冲区数据，触发数据接收回调
     */
    void update() override;
    
    /**
     * @brief 设置 APN（接入点名称）
     * @param apn APN 字符串
     */
    void setAPN(const char* apn);
    
    /**
     * @brief 建立 TCP 连接到服务器
     * @param server 服务器 IP 或域名
     * @param port 服务器端口
     * @return true-连接成功，false-失败
     * @details 设置 APN、激活 GPRS 附着、发起 TCP 连接
     */
    bool connectTCP(const char* server, uint16_t port);
    
    /**
     * @brief 获取信号强度
     * @return RSSI 值（0-31），-1 表示获取失败
     */
    int getSignalStrength();
    
    /**
     * @brief 获取 SIM 卡 ICCID
     * @param buffer 输出缓冲区（至少 21 字节）
     * @param bufferSize 缓冲区大小
     * @return true-获取成功，false-失败
     */
    bool getICCID(char* buffer, size_t bufferSize);
    
    /**
     * @brief 获取模块 IMEI
     * @param buffer 输出缓冲区（至少 16 字节）
     * @param bufferSize 缓冲区大小
     * @return true-获取成功，false-失败
     */
    bool getIMEI(char* buffer, size_t bufferSize);
    
private:
    HardwareSerial* _serial;      /**< 串口对象指针 */
    bool _tcpConnected;           /**< TCP 连接状态 */
    int _rxPin;                   /**< 接收引脚 */
    int _txPin;                   /**< 发送引脚 */
    int _pwrKeyPin;               /**< 电源控制引脚 */
    long _baudRate;               /**< 波特率 */
    char _apn[64];                /**< APN 名称 */
    char _server[128];            /**< 服务器地址 */
    uint16_t _port;               /**< 服务器端口 */
    
    /**
     * @brief 启动 4G 模块
     * @return true-开机成功，false-失败
     * @details 拉高 PWR_KEY 1.5s 后拉低，等待 "RDY" 响应
     */
    bool powerOn();
    
    /**
     * @brief 关闭 4G 模块
     * @return true-关机成功
     */
    bool powerOff();
    
    /**
     * @brief 等待串口响应
     * @param expected 期望的响应字符串
     * @param timeout 超时时间（毫秒）
     * @return true-收到期望响应，false-超时
     */
    bool waitForResponse(const char* expected, unsigned long timeout = 3000);
    
    /**
     * @brief 发送 AT 命令并等待响应
     * @param cmd AT 命令字符串
     * @param expected 期望的响应（默认 "OK"）
     * @param timeout 超时时间（毫秒）
     * @return true-收到期望响应，false-失败
     */
    bool sendATCommand(const char* cmd, const char* expected = "OK", unsigned long timeout = 3000);
};

#endif
