/**
 * @file Protocol.cpp
 * @brief SM-C0x 睡眠呼吸心跳雷达通信协议实现
 * @details 本文件实现了协议定义中的所有方法，包括JSON数据的构建和解析
 */

#include "Protocol.h"

/**
 * @brief 构造函数
 */
Protocol::Protocol() {
    memset(_imei, 0, sizeof(_imei));
    memset(_imsi, 0, sizeof(_imsi));
    memset(_iccid, 0, sizeof(_iccid));
    _signal = 99;
    memset(_swVersion, 0, sizeof(_swVersion));
    memset(_hdVersion, 0, sizeof(_hdVersion));
}

/**
 * @brief 设置设备标识信息
 * @param imei 设备IMEI号
 * @param imsi 设备IMSI号
 * @param iccid SIM卡ICCID号
 */
void Protocol::setDeviceInfo(const char* imei, const char* imsi, const char* iccid) {
    if (imei != nullptr) {
        strncpy(_imei, imei, sizeof(_imei) - 1);
    }
    if (imsi != nullptr) {
        strncpy(_imsi, imsi, sizeof(_imsi) - 1);
    }
    if (iccid != nullptr) {
        strncpy(_iccid, iccid, sizeof(_iccid) - 1);
    }
}

/**
 * @brief 设置信号强度
 * @param signal 信号强度值（0-31，99表示未获取到信号）
 */
void Protocol::setSignalStrength(uint8_t signal) {
    _signal = signal;
}

/**
 * @brief 设置固件版本
 * @param swVersion 软件版本
 * @param hdVersion 硬件版本
 */
void Protocol::setVersionInfo(const char* swVersion, const char* hdVersion) {
    if (swVersion != nullptr) {
        strncpy(_swVersion, swVersion, sizeof(_swVersion) - 1);
    }
    if (hdVersion != nullptr) {
        strncpy(_hdVersion, hdVersion, sizeof(_hdVersion) - 1);
    }
}

/**
 * @brief 获取当前时间戳（Unix时间戳）
 * @return 当前时间戳
 */
unsigned long Protocol::getTimestamp() {
    return millis() / 1000;
}

/**
 * @brief 构建JSON头部（包含通用字段）
 * @param dataType 数据类型
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 已写入的字节数
 */
int Protocol::buildJsonHeader(DataType dataType, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize < 128) {
        return 0;
    }
    
    unsigned long timestamp = getTimestamp();
    
    return snprintf(buffer, bufferSize, 
        "{\"dataType\":%d,\"deviceType\":\"%s\",\"sendTime\":%lu,\"IMEI\":\"%s\",\"IMSI\":\"%s\",\"ICCID\":\"%s\",\"signal\":%u",
        dataType, DEVICE_TYPE, timestamp, _imei, _imsi, _iccid, _signal);
}

/**
 * @brief 追加JSON尾部
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param currentLength 当前已写入长度
 * @return 最终长度
 */
int Protocol::appendJsonFooter(char* buffer, size_t bufferSize, int currentLength) {
    if (buffer == nullptr || currentLength <= 0 || currentLength >= (int)bufferSize) {
        return 0;
    }
    
    int result = snprintf(buffer + currentLength, bufferSize - currentLength, "}");
    
    if (result < 0 || currentLength + result >= (int)bufferSize) {
        return 0;
    }
    
    return currentLength + result;
}

/**
 * @brief 构建设备心跳信息上报JSON
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildHeartbeatJson(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize < 200) {
        return 0;
    }
    
    unsigned long timestamp = getTimestamp();
    
    return snprintf(buffer, bufferSize,
        "{\"dataType\":0,\"deviceType\":\"%s\",\"sendTime\":%lu,\"IMEI\":\"%s\",\"IMSI\":\"%s\",\"ICCID\":\"%s\",\"signal\":%u,\"hdVersion\":\"%s\",\"swVersion\":\"%s\"}",
        DEVICE_TYPE, timestamp, _imei, _imsi, _iccid, _signal, _hdVersion, _swVersion);
}

/**
 * @brief 构建设备参数信息上报JSON
 * @param params 设备参数
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildParamsJson(const DeviceParams* params, char* buffer, size_t bufferSize) {
    if (params == nullptr || buffer == nullptr || bufferSize < 300) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_PARAMS, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"detectionMode\":%d,\"heartRateSwitch\":%d,\"breathingSwitch\":%d,\"sleepSwitch\":%d,\"longTimeNoTimerSwitch\":%d,\"unmanneDuration\":%u,\"existSwitch\":%d,\"abnormalStruggleSwitch\":%d}",
        params->detectionMode,
        params->heartRateSwitch ? 1 : 0,
        params->breathingSwitch ? 1 : 0,
        params->sleepSwitch ? 1 : 0,
        params->longTimeNoTimerSwitch ? 1 : 0,
        params->unmanneDuration,
        params->existSwitch ? 1 : 0,
        params->abnormalStruggleSwitch ? 1 : 0);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 构建存在信息上报JSON
 * @param presence 存在信息
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildPresenceJson(const PresenceInfo* presence, char* buffer, size_t bufferSize) {
    if (presence == nullptr || buffer == nullptr || bufferSize < 300) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_PRESENCE, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"exist\":%d,\"movement\":%d,\"body_motion\":%u,\"bed\":%d,\"sleepStatus\":%d}",
        presence->exist ? 1 : 0,
        presence->movement,
        presence->bodyMotion,
        presence->bed,
        presence->sleepStatus);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 构建呼吸心率信息上报JSON
 * @param info 呼吸心率信息
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildBreathHeartJson(const BreathHeartInfo* info, char* buffer, size_t bufferSize) {
    if (info == nullptr || buffer == nullptr || bufferSize < 250) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_BREATH_HEART, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"heartRate\":%u,\"respiration\":%u,\"respirationStatus\":%d}",
        info->heartRate,
        info->respiration,
        info->respirationStatus);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 构建异常挣扎信息上报JSON
 * @param state 异常状态
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildAbnormalJson(AbnormalStatus state, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize < 200) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_ABNORMAL, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"abnormalStruggleState\":%d}",
        state);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 构建无人计时状态上报JSON
 * @param state 无人计时状态
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildNoPersonJson(AbnormalStatus state, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize < 200) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_NO_PERSON, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"untimedState\":%d}",
        state);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 构建睡眠报告上报JSON
 * @param report 睡眠报告
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildSleepReportJson(const SleepReport* report, char* buffer, size_t bufferSize) {
    if (report == nullptr || buffer == nullptr || bufferSize < 400) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_SLEEP_REPORT, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"sleepScore\":%u,\"sleepQuality\":%d,\"totalSleepDuration\":%u,\"lengthWakefulness\":%u,\"lightSleepDuration\":%u,\"deepSleepDuration\":%u,\"meanSleepRespiration\":%u,\"sleepMeanHeartbeat\":%u,\"numberdEparturesBed\":%u,\"numberTurns\":%u}",
        report->sleepScore,
        report->sleepQuality,
        report->totalSleepDuration,
        report->lengthWakefulness,
        report->lightSleepDuration,
        report->deepSleepDuration,
        report->meanSleepRespiration,
        report->sleepMeanHeartbeat,
        report->numberdEparturesBed,
        report->numberTurns);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 构建指令下发应答JSON
 * @param cmdID 指令ID
 * @param result 指令执行结果
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildCommandResponseJson(CommandID cmdID, CommandResult result, char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize < 200) {
        return 0;
    }
    
    unsigned long timestamp = getTimestamp();
    
    return snprintf(buffer, bufferSize,
        "{\"dataType\":7,\"sendTime\":%lu,\"IMEI\":\"%s\",\"deviceType\":\"%s\",\"cmdID\":\"%d\",\"cmdResult\":%d}",
        timestamp, _imei, DEVICE_TYPE, cmdID, result);
}

/**
 * @brief 构建睡眠综合状态上报JSON
 * @param summary 睡眠综合状态
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @return 生成的JSON长度，0表示失败
 */
int Protocol::buildSleepSummaryJson(const ProtocolSleepSummary* summary, char* buffer, size_t bufferSize) {
    if (summary == nullptr || buffer == nullptr || bufferSize < 350) {
        return 0;
    }
    
    int len = buildJsonHeader(DATA_TYPE_SLEEP_SUMMARY, buffer, bufferSize);
    if (len <= 0) {
        return 0;
    }
    
    int result = snprintf(buffer + len, bufferSize - len,
        ",\"exist\":%d,\"sleepStatus\":%d,\"meanSleepRespiration\":%u,\"sleepMeanHeartbeat\":%u,\"numberTurns\":%u,\"max_body_motion\":%u,\"min_body_motion\":%u,\"respiration_stop_num\":%u}",
        summary->exist ? 1 : 0,
        summary->sleepStatus,
        summary->meanSleepRespiration,
        summary->sleepMeanHeartbeat,
        summary->numberTurns,
        summary->maxBodyMotion,
        summary->minBodyMotion,
        summary->respirationStopNum);
    
    if (result < 0 || len + result >= (int)bufferSize) {
        return 0;
    }
    
    return len + result;
}

/**
 * @brief 解析字符串为整数
 * @param str 输入字符串
 * @param defaultValue 默认值
 * @return 解析后的整数
 */
static int parseInt(const char* str, int defaultValue) {
    if (str == nullptr || str[0] == '\0') {
        return defaultValue;
    }
    return atoi(str);
}

/**
 * @brief 在JSON字符串中查找键值对
 * @param json JSON字符串
 * @param key 要查找的键
 * @param value 输出值缓冲区
 * @param valueSize 缓冲区大小
 * @return true-找到，false-未找到
 */
static bool findJsonValue(const char* json, const char* key, char* value, size_t valueSize) {
    if (json == nullptr || key == nullptr || value == nullptr) {
        return false;
    }
    
    char keyPattern[64];
    snprintf(keyPattern, sizeof(keyPattern), "\"%s\":", key);
    
    const char* pos = strstr(json, keyPattern);
    if (pos == nullptr) {
        return false;
    }
    
    pos += strlen(keyPattern);
    
    // 跳过空格
    while (*pos == ' ') {
        pos++;
    }
    
    // 判断是否是字符串（带引号）
    if (*pos == '\"') {
        pos++;
        const char* end = strchr(pos, '\"');
        if (end == nullptr) {
            return false;
        }
        size_t len = end - pos;
        if (len >= valueSize) {
            len = valueSize - 1;
        }
        strncpy(value, pos, len);
        value[len] = '\0';
        return true;
    }
    
    // 处理数值类型（跳过空白字符）
    const char* end = pos;
    while (*end != '\0' && *end != ',' && *end != '}' && !isspace((unsigned char)*end)) {
        end++;
    }
    
    size_t len = end - pos;
    if (len >= valueSize) {
        len = valueSize - 1;
    }
    strncpy(value, pos, len);
    value[len] = '\0';
    return true;
}

/**
 * @brief 解析下行指令JSON
 * @param jsonData JSON数据
 * @param command 输出指令结构体
 * @return true-解析成功，false-解析失败
 */
bool Protocol::parseDownlinkCommand(const char* jsonData, DownlinkCommand* command) {
    if (jsonData == nullptr || command == nullptr) {
        return false;
    }
    
    memset(command, 0, sizeof(DownlinkCommand));
    
    char value[64];
    
    // 解析cmdID
    if (!findJsonValue(jsonData, "cmdID", value, sizeof(value))) {
        return false;
    }
    command->cmdID = (CommandID)parseInt(value, -1);
    
    // 解析sendTime（可选字段）
    if (findJsonValue(jsonData, "sendTime", value, sizeof(value))) {
        command->sendTime = (unsigned long)parseInt(value, 0);
    } else {
        command->sendTime = millis();  // 使用当前时间作为默认值
    }
    
    // 如果是设置参数指令，解析参数
    if (command->cmdID == CMD_ID_SET_PARAMS) {
        if (findJsonValue(jsonData, "detectionMode", value, sizeof(value))) {
            command->params.detectionMode = (DetectMode)parseInt(value, DETECT_MODE_REAL_TIME);
            command->paramsMask.detectionMode = true;
        }
        
        if (findJsonValue(jsonData, "heartRateSwitch", value, sizeof(value))) {
            command->params.heartRateSwitch = (parseInt(value, 0) == 1);
            command->paramsMask.heartRateSwitch = true;
        }
        
        if (findJsonValue(jsonData, "breathingSwitch", value, sizeof(value))) {
            command->params.breathingSwitch = (parseInt(value, 0) == 1);
            command->paramsMask.breathingSwitch = true;
        }
        
        if (findJsonValue(jsonData, "sleepSwitch", value, sizeof(value))) {
            command->params.sleepSwitch = (parseInt(value, 0) == 1);
            command->paramsMask.sleepSwitch = true;
        }
        
        if (findJsonValue(jsonData, "longTimeNoTimerSwitch", value, sizeof(value))) {
            command->params.longTimeNoTimerSwitch = (parseInt(value, 0) == 1);
            command->paramsMask.longTimeNoTimerSwitch = true;
        }
        
        if (findJsonValue(jsonData, "unmanneDuration", value, sizeof(value))) {
            command->params.unmanneDuration = (uint16_t)parseInt(value, 60);
            command->paramsMask.unmanneDuration = true;
        }
        
        if (findJsonValue(jsonData, "existSwitch", value, sizeof(value))) {
            command->params.existSwitch = (parseInt(value, 0) == 1);
            command->paramsMask.existSwitch = true;
        }
        
        if (findJsonValue(jsonData, "abnormalStruggleSwitch", value, sizeof(value))) {
            command->params.abnormalStruggleSwitch = (parseInt(value, 0) == 1);
            command->paramsMask.abnormalStruggleSwitch = true;
        }
    }
    
    // 如果是OTA升级指令，解析相关参数
    if (command->cmdID == CMD_ID_OTA_UPDATE) {
        findJsonValue(jsonData, "versionOld", command->versionOld, sizeof(command->versionOld));
        findJsonValue(jsonData, "versionNew", command->versionNew, sizeof(command->versionNew));
        findJsonValue(jsonData, "hdVersion", command->hdVersion, sizeof(command->hdVersion));
        findJsonValue(jsonData, "otaUrl", command->otaUrl, sizeof(command->otaUrl));
    }
    
    return true;
}