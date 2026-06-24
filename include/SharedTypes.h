/**
 * @file SharedTypes.h
 * @brief 共享类型定义头文件
 * @details 用于解决 RadarR60A.h 和 Protocol.h 之间的循环依赖问题
 */

#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include <Arduino.h>

/**
 * @brief 睡眠状态枚举
 * @note 官方定义：3-离床(无人), 2-清醒, 1-浅睡, 0-深睡
 */
typedef enum {
    SLEEP_DEEP = 0,       // 深睡眠
    SLEEP_LIGHT = 1,      // 浅睡眠
    SLEEP_AWAKE = 2,      // 清醒状态
    SLEEP_AWAY = 3        // 离床/无人
} SleepState;

/**
 * @brief 运动状态枚举
 */
typedef enum {
    MOVEMENT_NONE = 0,    // 无
    MOVEMENT_STATIC = 1,  // 静止
    MOVEMENT_ACTIVE = 2   // 活跃
} MovementState;

/**
 * @brief 呼吸状态枚举
 */
typedef enum {
    BREATH_NORMAL = 1,    // 正常
    BREATH_HIGH = 2,      // 呼吸过高
    BREATH_LOW = 3,       // 呼吸过低
    BREATH_NONE = 4       // 无人时的默认状态
} BreathStatus;

/**
 * @brief 探测模式枚举
 */
typedef enum {
    DETECT_MODE_REAL_TIME = 0,    // 实时探测模式
    DETECT_MODE_SLEEP = 1         // 睡眠探测模式
} DetectMode;

/**
 * @brief 设备参数结构体
 */
typedef struct {
    DetectMode detectionMode;           // 探测模式
    bool heartRateSwitch;               // 心率开关
    bool breathingSwitch;               // 呼吸开关
    bool sleepSwitch;                   // 睡眠开关
    bool longTimeNoTimerSwitch;         // 长时间无人计时开关
    uint16_t unmanneDuration;           // 无人计时时长（30~180分钟）
    bool existSwitch;                   // 存在开关
    bool abnormalStruggleSwitch;        // 异常挣扎开关
} DeviceParams;

#endif // SHARED_TYPES_H