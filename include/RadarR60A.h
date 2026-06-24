/**
 * @file RadarR60A.h
 * @brief R60ABD1 60GHz毫米波雷达模块驱动头文件
 * @details 基于 MicRadar R60ABD1 官方技术手册V3.3设计
 */

#ifndef RADAR_R60A_H
#define RADAR_R60A_H

#include <Arduino.h>
#include "SharedTypes.h"

/**
 * @brief 数据包解析状态机枚举
 */
typedef enum {
    STATE_IDLE = 0,       // 空闲状态
    STATE_STX1 = 1,       // 帧头第一个字节
    STATE_CONTROL = 2,    // 控制字状态
    STATE_COMMAND = 3,    // 命令字状态
    STATE_LENGTH_H = 4,   // 长度高字节
    STATE_LENGTH_L = 5,   // 长度低字节
    STATE_DATA = 6,       // 数据状态
    STATE_CHECKSUM = 7,   // 校验和状态
    STATE_TC1 = 8,        // 帧尾第一个字节
    STATE_TC2 = 9         // 帧尾第二个字节
} ParseState;

/**
 * @brief 睡眠综合状态结构体
 */
typedef struct {
    uint8_t presence;              // 存在状态: 1-有人, 0-无人
    uint8_t sleepState;            // 睡眠状态
    uint8_t avgHeartRate;          // 平均心率
    uint8_t avgBreathRate;         // 平均呼吸
    uint16_t rollOverCount;        // 翻身次数
    uint8_t largeMovementRatio;    // 大幅度体动占比
    uint8_t smallMovementRatio;    // 小幅度体动占比
} SleepSummary;

/**
 * @brief R60ABD1雷达类
 */
class RadarR60A {
public:
    /**
     * @brief 雷达数据结构体
     */
    typedef struct {
        bool isDetected;              // 是否检测到人体
        uint8_t breathRate;           // 呼吸频率 (0-35 bpm)
        uint8_t heartRate;            // 心率 (60-120 bpm)
        uint16_t distance;            // 探测距离 (0-65535 cm)
        uint8_t bodyMovement;         // 体动幅度参数 (0-100)
        MovementState movementState;  // 运动状态
        SleepState sleepState;        // 睡眠状态
        BreathStatus breathStatus;    // 呼吸状态
        int16_t positionX;            // X坐标 (cm)
        int16_t positionY;            // Y坐标 (cm)
        int16_t positionZ;            // Z坐标 (cm)
        uint8_t breathWave[5];        // 呼吸波形 (每秒5个值)
        uint8_t heartWave[5];         // 心率波形 (每秒5个值)
        uint16_t awakeDuration;       // 清醒时长 (分钟)
        uint16_t lightSleepDuration;  // 浅睡时长 (分钟)
        uint16_t deepSleepDuration;   // 深睡时长 (分钟)
        uint8_t sleepQuality;         // 睡眠质量评分 (0-100)
        SleepSummary sleepSummary;    // 睡眠综合状态
    } RadarData;

    /**
     * @brief 控制字常量定义
     */
    static const uint8_t CTRL_HEARTBEAT = 0x01;        // 心跳包标识
    static const uint8_t CTRL_PRODUCT_INFO = 0x02;     // 产品信息
    static const uint8_t CTRL_OTA = 0x03;              // OTA升级
    static const uint8_t CTRL_STATUS = 0x05;           // 工作状态
    static const uint8_t CTRL_RANGE = 0x07;            // 雷达探测范围信息
    static const uint8_t CTRL_HUMAN_PRESENCE = 0x80;   // 人体存在
    static const uint8_t CTRL_BREATH = 0x81;           // 呼吸检测
    static const uint8_t CTRL_SLEEP = 0x84;            // 睡眠监测
    static const uint8_t CTRL_HEART_RATE = 0x85;       // 心率监测
    // 别名定义，保持代码兼容性
    static const uint8_t CTRL_SYSTEM = CTRL_HEARTBEAT;         // 系统功能（复用心跳包控制字）
    static const uint8_t CTRL_SLEEP_MONITOR = CTRL_SLEEP;     // 睡眠监测（别名）

    /**
     * @brief 命令字常量定义
     */
    static const uint8_t CMD_PRESENCE_DETECT = 0x01;   // 有人/无人状态
    static const uint8_t CMD_MOVEMENT_STATE = 0x02;    // 静止/活跃状态
    static const uint8_t CMD_BODY_MOVEMENT = 0x03;     // 体动参数
    static const uint8_t CMD_DISTANCE = 0x04;          // 人体距离
    static const uint8_t CMD_POSITION = 0x05;          // 人体方位
    static const uint8_t CMD_BREATH_INFO = 0x01;       // 呼吸信息
    static const uint8_t CMD_BREATH_RATE = 0x02;       // 呼吸数值
    static const uint8_t CMD_BREATH_WAVE = 0x05;       // 呼吸波形
    static const uint8_t CMD_HEART_RATE = 0x02;        // 心率数值
    static const uint8_t CMD_HEART_WAVE = 0x05;        // 心率波形
    static const uint8_t CMD_SLEEP_STATE = 0x01;       // 睡眠状态
    static const uint8_t CMD_SLEEP_DURATION = 0x02;    // 睡眠时长
    static const uint8_t CMD_SLEEP_QUALITY = 0x03;     // 睡眠质量评分
    static const uint8_t CMD_SLEEP_SUMMARY = 0x04;     // 睡眠综合状态
    
    // 功能开关命令字
    static const uint8_t CMD_FUNC_ENABLE = 0x00;       // 功能开启命令字
    static const uint8_t CMD_FUNC_DISABLE = 0x80;      // 功能关闭命令字（带0x80前缀）

    /**
     * @brief 缓冲区大小常量
     */
    static const uint8_t MAX_BUFFER_SIZE = 128;        // 最大数据包缓冲区

    /**
     * @brief 构造函数
     * @param rxPin ESP32接收引脚
     * @param txPin ESP32发送引脚
     * @param baudRate 串口波特率（默认115200）
     */
    RadarR60A(int rxPin, int txPin, long baudRate = 115200);

    /**
     * @brief 初始化雷达模块
     * @details 配置串口参数并发送初始化命令，包含完整的初始化确认机制
     * @return true-初始化成功，false-初始化失败（超时）
     */
    bool begin();

    /**
     * @brief 更新雷达数据
     */
    void update();

    /**
     * @brief 获取当前雷达数据
     * @return RadarData结构体
     */
    RadarData getData();

    /**
     * @brief 打印雷达数据到串口
     */
    void printData();

    /**
     * @brief 发送JSON格式数据到串口
     */
    void sendJsonData();

    /**
     * @brief 复位雷达模块
     * @details 发送复位命令使雷达模块重启，用于初始化或异常恢复
     * @param timeout 等待复位的超时时间（毫秒），默认 2000ms
     * @return true-复位成功，false-超时未响应
     */
    bool resetModule(unsigned long timeout = 2000);

    /**
     * @brief 开启人体存在检测
     * @return true-发送成功，false-发送失败
     */
    bool enableHumanPresence();

    /**
     * @brief 关闭人体存在检测
     * @return true-发送成功，false-发送失败
     */
    bool disableHumanPresence();

    /**
     * @brief 开启呼吸检测
     * @return true-发送成功，false-发送失败
     */
    bool enableBreathDetection();

    /**
     * @brief 关闭呼吸检测
     * @return true-发送成功，false-发送失败
     */
    bool disableBreathDetection();

    /**
     * @brief 开启心率检测
     * @return true-发送成功，false-发送失败
     */
    bool enableHeartRateDetection();

    /**
     * @brief 关闭心率检测
     * @return true-发送成功，false-发送失败
     */
    bool disableHeartRateDetection();

    /**
     * @brief 开启睡眠监测
     * @return true-发送成功，false-发送失败
     */
    bool enableSleepMonitoring();

    /**
     * @brief 关闭睡眠监测
     * @return true-发送成功，false-发送失败
     */
    bool disableSleepMonitoring();

    /**
     * @brief 根据设备参数配置雷达检测功能
     * @param params 设备参数结构体指针
     * @return true-配置成功，false-配置失败
     * @details 根据设备参数中的各个开关状态，向雷达模块发送对应的控制命令，
     *          实现数据上报类型与雷达功能的联动。
     */
    bool configureDetection(const DeviceParams* params);

    /**
     * @brief 查询产品型号
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return true-查询成功，false-查询失败
     */
    bool queryProductModel(char* buffer, size_t bufferSize);

    /**
     * @brief 查询产品ID
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return true-查询成功，false-查询失败
     */
    bool queryProductId(char* buffer, size_t bufferSize);

    /**
     * @brief 查询硬件型号
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return true-查询成功，false-查询失败
     */
    bool queryHardwareModel(char* buffer, size_t bufferSize);

    /**
     * @brief 查询固件版本
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return true-查询成功，false-查询失败
     */
    bool queryFirmwareVersion(char* buffer, size_t bufferSize);

    /**
     * @brief 查询初始化状态
     * @return true-已初始化，false-未初始化或查询失败
     */
    bool queryInitStatus();

    /**
     * @brief 查询位置越界状态
     * @return true-范围内，false-范围外或查询失败
     */
    bool queryPositionStatus();

    /**
     * @brief 设置探测模式
     * @param mode 探测模式：0-实时探测模式，1-睡眠探测模式
     * @return true-设置成功，false-设置失败
     */
    bool setDetectionMode(uint8_t mode);

    /**
     * @brief 设置心率波形上报开关
     * @param enable true-开启，false-关闭
     * @return true-设置成功，false-设置失败
     * @details 心率波形上报默认关闭，开启后每秒上报 5 个心率波形数据点
     */
    bool enableHeartRateWaveReport(bool enable);

    /**
     * @brief 设置呼吸波形上报开关
     * @param enable true-开启，false-关闭
     * @return true-设置成功，false-设置失败
     * @details 呼吸波形上报默认关闭，开启后每秒上报 5 个呼吸波形数据点
     */
    bool enableBreathWaveReport(bool enable);

    /**
     * @brief 查询人体存在开关状态
     * @return true-已开启，false-已关闭或查询失败
     */
    bool queryHumanPresenceSwitch();

    /**
     * @brief 查询心率监测开关状态
     * @return true-已开启，false-已关闭或查询失败
     */
    bool queryHeartRateSwitch();

    /**
     * @brief 查询呼吸监测开关状态
     * @return true-已开启，false-已关闭或查询失败
     */
    bool queryBreathSwitch();

    /**
     * @brief 查询人体距离
     * @return 距离值（cm），0-查询失败或无人
     */
    uint16_t queryHumanDistance();

    /**
     * @brief 查询人体方位信息
     * @param x x 轴坐标（cm），有正负
     * @param y y 轴坐标（cm），有正负
     * @param z z 轴坐标（cm），有正负
     * @return true-查询成功，false-查询失败
     */
    bool queryHumanPosition(int16_t* x, int16_t* y, int16_t* z);

    /**
     * @brief 设置异常挣扎检测开关
     * @param enable true-开启，false-关闭
     * @return true-设置成功，false-设置失败
     */
    bool enableAbnormalStruggleDetection(bool enable);

    /**
     * @brief 设置无人计时功能开关
     * @param enable true-开启，false-关闭
     * @return true-设置成功，false-设置失败
     */
    bool enableLongTimeNoTimer(bool enable);

    /**
     * @brief 设置无人计时时长
     * @param duration 计时时长（30~180分钟）
     * @return true-设置成功，false-设置失败
     */
    bool setLongTimeNoTimerDuration(uint8_t duration);

    /**
     * @brief 设置呼吸过低判读阈值
     * @param threshold 呼吸阈值（次/min）
     * @return true-设置成功，false-设置失败
     */
    bool setBreathLowThreshold(uint8_t threshold);

    /**
     * @brief 查询睡眠监测开关状态
     * @return true-已开启，false-已关闭或查询失败
     */
    bool querySleepSwitch();

    /**
     * @brief 查询入床离床状态
     * @return 0-离床，1-入床，2-无
     */
    uint8_t queryBedStatus();

    /**
     * @brief 查询睡眠异常状态
     * @return 0-无异常，1-睡眠时长异常，2-无人异常
     */
    uint8_t querySleepAbnormalStatus();

    /**
     * @brief 雷达模组软复位（通过命令方式）
     * @details 发送复位命令使雷达模块重启，不同于 resetModule() 的硬件复位方式
     * @return true-复位成功，false-复位失败
     */
    bool softResetModule();

    /**
     * @brief 开始OTA升级
     * @param version 新版本号
     * @return true-成功，false-失败
     */
    bool startOtaUpgrade(const char* version);

private:
    int _rxPin;                   // 接收引脚
    int _txPin;                   // 发送引脚
    long _baudRate;               // 波特率
    HardwareSerial* _serial;      // 串口对象指针
    uint8_t _buffer[MAX_BUFFER_SIZE];  // 数据缓冲区
    uint8_t _bufferIndex;         // 缓冲区索引
    ParseState _state;            // 解析状态机状态
    uint8_t _controlWord;         // 当前控制字
    uint8_t _commandWord;         // 当前命令字
    uint16_t _dataLength;         // 当前数据包数据长度
    uint8_t _dataCount;           // 当前已接收数据计数
    uint8_t _receivedChecksum;    // 接收到的校验和
    RadarData _currentData;       // 当前雷达数据
    bool _initialized;            // 初始化标志
    bool _heartbeatReceived;      // 心跳包接收标志
    unsigned long _initStartTime; // 初始化开始时间
    static const unsigned long INIT_TIMEOUT = 5000;  // 初始化超时时间（5秒）

    /**
     * @brief 发送心跳包查询命令
     */
    void sendHeartbeatQuery();


    /**
     * @brief 查询产品信息（固件版本、产品型号、硬件型号、产品 ID）
     */
    void queryProductInfo();

    /**
     * @brief 计算产品信息查询命令的校验和
     * @param data 数据指针
     * @param length 数据长度
     * @return 校验和（低 8 位）
     */
    uint8_t calculateProductInfoChecksum(uint8_t* data, uint16_t length);

    /**
     * @brief 解析产品信息数据
     */
    void parseProductInfo();

    /**
     * @brief 读取串口数据
     */
    void readSerial();

    /**
     * @brief 计算校验和
     * @param data 数据指针
     * @param length 数据长度
     * @return 校验和（低 8 位）
     */
    uint8_t calculateChecksum(uint8_t* data, uint16_t length);

    /**
     * @brief 显示校验和计算过程（调试用）
     * @param data 数据指针
     * @param length 数据长度
     */
    void debugChecksum(uint8_t* data, uint16_t length);

    /**
     * @brief 解析数据包
     */
    void parsePacket();

    /**
     * @brief 解析人体存在数据
     */
    void parseHumanPresenceData();

    /**
     * @brief 解析呼吸数据
     */
    void parseBreathData();

    /**
     * @brief 解析心率数据
     */
    void parseHeartRateData();

    /**
     * @brief 解析睡眠数据
     */
    void parseSleepData();

    /**
     * @brief 解析心跳包数据
     */
    void parseHeartbeatData();

    /**
     * @brief 解析工作状态数据
     */
    void parseStatusData();

    /**
     * @brief 解码位置坐标
     * @param rawValue 原始16位数据
     * @return 解码后的坐标值（cm）
     */
    int16_t decodePosition(int16_t rawValue);

    /**
     * @brief 发送命令到雷达模块
     * @param control 控制字
     * @param command 命令字
     * @param data 数据指针
     * @param dataLen 数据长度
     */
    void sendCommand(uint8_t control, uint8_t command, uint8_t* data, uint8_t dataLen);
};

#endif // RADAR_R60A_H
