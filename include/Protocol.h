/**
 * @file Protocol.h
 * @brief SM-C0x 睡眠呼吸心跳雷达通信协议定义
 * @details 本文件基于中国电信天翼物联平台协议规范，定义了完整的数据上传与下发通信协议：
 *          - 数据帧结构（JSON格式）
 *          - 上行通信指令集（设备→服务器）
 *          - 下行通信指令集（服务器→设备）
 *          - 错误处理机制
 *          - 通信时序规范
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "SharedTypes.h"

/**
 * @brief 协议版本号
 */
#define PROTOCOL_VERSION_MAJOR 1
#define PROTOCOL_VERSION_MINOR 0

/**
 * @brief 设备类型标识
 */
#define DEVICE_TYPE "sleepRadar"

/**
 * @brief 数据类型枚举（上行）
 */
typedef enum {
    DATA_TYPE_HEARTBEAT = 0,      // 设备心跳信息上报
    DATA_TYPE_PARAMS = 1,         // 设备参数信息上报
    DATA_TYPE_PRESENCE = 2,       // 存在信息上报
    DATA_TYPE_BREATH_HEART = 3,   // 呼吸心率信息上报
    DATA_TYPE_ABNORMAL = 4,       // 异常挣扎信息上报
    DATA_TYPE_NO_PERSON = 5,      // 无人计时状态上报
    DATA_TYPE_SLEEP_REPORT = 6,   // 睡眠报告上报
    DATA_TYPE_CMD_RESPONSE = 7,   // 指令下发应答上报
    DATA_TYPE_SLEEP_SUMMARY = 8   // 睡眠综合状态上报
} DataType;

/**
 * @brief 指令ID枚举（下行）
 */
typedef enum {
    CMD_ID_SET_PARAMS = 0,        // 设置参数
    CMD_ID_RESET_MODULE = 1,      // 复位模组
    CMD_ID_REBOOT_DEVICE = 2,     // 重启设备
    CMD_ID_OTA_UPDATE = 3         // 设备差分升级（暂不实现）
} CommandID;

/**
 * @brief 指令执行结果枚举
 */
typedef enum {
    CMD_RESULT_ERROR = 0,         // 指令错误
    CMD_RESULT_SUCCESS = 1,       // 指令成功
    CMD_RESULT_REJECTED = 2       // 指令拒绝执行
} CommandResult;

/**
 * @brief 睡眠状态枚举（协议层使用，与 SharedTypes 中的 SleepState 区分）
 */
typedef enum {
    SLEEP_STATUS_DEEP = 0,        // 深睡
    SLEEP_STATUS_LIGHT = 1,       // 浅睡
    SLEEP_STATUS_AWAKE = 2,       // 清醒
    SLEEP_STATUS_NONE = 3         // 无
} SleepStatus;

/**
 * @brief 离床状态枚举
 */
typedef enum {
    BED_STATUS_AWAY = 0,          // 离床
    BED_STATUS_IN = 1,            // 入床
    BED_STATUS_NONE = 2           // 无
} BedStatus;

/**
 * @brief 异常状态枚举
 */
typedef enum {
    ABNORMAL_NONE = 0,            // 无
    ABNORMAL_NORMAL = 1,          // 正常
    ABNORMAL_DETECTED = 2         // 异常
} AbnormalStatus;

/**
 * @brief 睡眠质量评级枚举
 */
typedef enum {
    SLEEP_QUALITY_NONE = 0,       // 无
    SLEEP_QUALITY_GOOD = 1,       // 良好
    SLEEP_QUALITY_NORMAL = 2,     // 一般
    SLEEP_QUALITY_POOR = 3        // 较差
} SleepQuality;

/**
 * @brief 存在信息结构体
 */
typedef struct {
    bool exist;                         // 有人/无人
    MovementState movement;             // 运动状态（来自RadarR60A）
    uint8_t bodyMotion;                 // 体动幅度（0~100）
    BedStatus bed;                      // 离床状态
    SleepStatus sleepStatus;            // 睡眠状态
} PresenceInfo;

/**
 * @brief 呼吸心率信息结构体
 */
typedef struct {
    uint8_t heartRate;                  // 心率（bpm）
    uint8_t respiration;                // 呼吸次数（次/min）
    BreathStatus respirationStatus;     // 呼吸状态（来自RadarR60A）
} BreathHeartInfo;

/**
 * @brief 睡眠报告结构体
 */
typedef struct {
    uint8_t sleepScore;                 // 睡眠评分（0-100）
    SleepQuality sleepQuality;          // 睡眠评级
    uint16_t totalSleepDuration;        // 睡眠总时长（min）
    uint16_t lengthWakefulness;         // 清醒时长（min）
    uint16_t lightSleepDuration;        // 浅睡时长（min）
    uint16_t deepSleepDuration;         // 深睡时长（min）
    uint8_t meanSleepRespiration;       // 睡眠平均呼吸（次/min）
    uint8_t sleepMeanHeartbeat;         // 睡眠平均心跳
    uint8_t numberdEparturesBed;        // 离床次数
    uint16_t numberTurns;               // 翻身次数
} SleepReport;

/**
 * @brief 协议层睡眠综合状态结构体（与RadarR60A中的SleepSummary区分）
 */
typedef struct {
    bool exist;                         // 有人/无人
    SleepStatus sleepStatus;            // 睡眠状态
    uint8_t meanSleepRespiration;       // 睡眠平均呼吸（次/min）
    uint8_t sleepMeanHeartbeat;         // 睡眠平均心跳
    uint16_t numberTurns;               // 翻身次数
    uint8_t maxBodyMotion;              // 大幅度体动占比
    uint8_t minBodyMotion;              // 小幅度体动占比
    uint8_t respirationStopNum;         // 呼吸暂停次数
} ProtocolSleepSummary;

/**
 * @brief 参数设置掩码结构体
 * @details 用于标识哪些参数在JSON中被实际设置
 */
typedef struct {
    bool detectionMode : 1;             // 探测模式是否被设置
    bool heartRateSwitch : 1;           // 心率开关是否被设置
    bool breathingSwitch : 1;           // 呼吸开关是否被设置
    bool sleepSwitch : 1;               // 睡眠开关是否被设置
    bool longTimeNoTimerSwitch : 1;     // 无人计时开关是否被设置
    bool unmanneDuration : 1;           // 无人计时时长是否被设置
    bool existSwitch : 1;               // 存在开关是否被设置
    bool abnormalStruggleSwitch : 1;    // 异常挣扎开关是否被设置
} ParamsMask;

/**
 * @brief 下行命令结构体
 */
typedef struct {
    CommandID cmdID;                    // 指令ID
    unsigned long sendTime;             // 下发时间戳
    DeviceParams params;                // 参数设置（cmdID=0时有效）
    ParamsMask paramsMask;              // 参数设置掩码
    char otaUrl[256];                  // OTA升级URL（cmdID=3时有效）
    char versionOld[16];               // 旧版本号
    char versionNew[16];               // 新版本号
    char hdVersion[16];                // 硬件版本号
} DownlinkCommand;

/**
 * @brief 通信协议类
 */
class Protocol {
public:
    /**
     * @brief 构造函数
     */
    Protocol();
    
    /**
     * @brief 设置设备标识信息
     * @param imei 设备IMEI号
     * @param imsi 设备IMSI号
     * @param iccid SIM卡ICCID号
     */
    void setDeviceInfo(const char* imei, const char* imsi, const char* iccid);
    
    /**
     * @brief 设置信号强度
     * @param signal 信号强度值（0-31，99表示未获取到信号）
     */
    void setSignalStrength(uint8_t signal);
    
    /**
     * @brief 设置固件版本
     * @param swVersion 软件版本
     * @param hdVersion 硬件版本
     */
    void setVersionInfo(const char* swVersion, const char* hdVersion);
    
    /**
     * @brief 构建设备心跳信息上报JSON
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildHeartbeatJson(char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建设备参数信息上报JSON
     * @param params 设备参数
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildParamsJson(const DeviceParams* params, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建存在信息上报JSON
     * @param presence 存在信息
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildPresenceJson(const PresenceInfo* presence, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建呼吸心率信息上报JSON
     * @param info 呼吸心率信息
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildBreathHeartJson(const BreathHeartInfo* info, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建异常挣扎信息上报JSON
     * @param state 异常状态
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildAbnormalJson(AbnormalStatus state, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建无人计时状态上报JSON
     * @param state 无人计时状态
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildNoPersonJson(AbnormalStatus state, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建睡眠报告上报JSON
     * @param report 睡眠报告
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildSleepReportJson(const SleepReport* report, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建指令下发应答JSON
     * @param cmdID 指令ID
     * @param result 指令执行结果
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildCommandResponseJson(CommandID cmdID, CommandResult result, char* buffer, size_t bufferSize);
    
    /**
     * @brief 构建睡眠综合状态上报JSON
     * @param summary 睡眠综合状态
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 生成的JSON长度，0表示失败
     */
    int buildSleepSummaryJson(const ProtocolSleepSummary* summary, char* buffer, size_t bufferSize);
    
    /**
     * @brief 解析下行指令JSON
     * @param jsonData JSON数据
     * @param command 输出指令结构体
     * @return true-解析成功，false-解析失败
     */
    bool parseDownlinkCommand(const char* jsonData, DownlinkCommand* command);
    
    /**
     * @brief 获取当前时间戳（Unix时间戳）
     * @return 当前时间戳
     */
    unsigned long getTimestamp();

private:
    char _imei[16];                     // 设备IMEI号
    char _imsi[16];                     // 设备IMSI号
    char _iccid[21];                    // SIM卡ICCID号
    uint8_t _signal;                    // 信号强度
    char _swVersion[16];                // 软件版本
    char _hdVersion[16];                // 硬件版本
    
    /**
     * @brief 构建JSON头部（包含通用字段）
     * @param dataType 数据类型
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @return 已写入的字节数
     */
    int buildJsonHeader(DataType dataType, char* buffer, size_t bufferSize);
    
    /**
     * @brief 追加JSON尾部
     * @param buffer 输出缓冲区
     * @param bufferSize 缓冲区大小
     * @param currentLength 当前已写入长度
     * @return 最终长度
     */
    int appendJsonFooter(char* buffer, size_t bufferSize, int currentLength);
};

#endif // PROTOCOL_H