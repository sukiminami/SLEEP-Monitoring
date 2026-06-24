/**
 * @file DataSender.cpp
 * @brief 数据发送模块实现
 * @details 实现将雷达数据按照SM-C0x协议规范打包并发送到服务器的功能
 */

#include "DataSender.h"

/**
 * @brief 构造函数
 * @param protocol 协议模块引用
 * @param networkManager 网络管理器引用
 */
DataSender::DataSender(Protocol& protocol, NetworkManager& networkManager)
    : _protocol(protocol), _networkManager(networkManager) {
}

/**
 * @brief 检查网络是否就绪
 * @return true-网络就绪，false-网络未就绪
 */
bool DataSender::isNetworkReady() {
    return _networkManager.isConnected();
}

/**
 * @brief 发送JSON数据到网络
 * @param jsonData JSON数据
 * @param length 数据长度
 * @param tag 日志标签
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendJson(const char* jsonData, int length, const char* tag) {
    if (length <= 0 || !isNetworkReady()) {
        return false;
    }
    
    Serial0.print("[");
    Serial0.print(tag);
    Serial0.print("] 发布数据：");
    Serial0.println(jsonData);
    
    bool success = _networkManager.send((const uint8_t*)jsonData, length);
    Serial0.print("[");
    Serial0.print(tag);
    Serial0.print("] 发送");
    Serial0.println(success ? "成功" : "失败");
    
    return success;
}

/**
 * @brief 发送心跳信息
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendHeartbeat() {
    char jsonBuffer[256];
    int len = _protocol.buildHeartbeatJson(jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "心跳");
}

/**
 * @brief 发送设备参数信息
 * @param params 设备参数
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendDeviceParams(const DeviceParams& params) {
    char jsonBuffer[350];
    int len = _protocol.buildParamsJson(&params, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "设备参数");
}

/**
 * @brief 发送存在信息
 * @param radarData 雷达数据
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendPresenceInfo(const RadarR60A::RadarData& radarData) {
    // 如果检测到有人，或者有呼吸/心率数据（说明有人），则认为存在
    bool isPresent = radarData.isDetected || radarData.breathRate > 0 || radarData.heartRate > 0;
    
    PresenceInfo presence = {
        .exist = isPresent,
        .movement = (MovementState)radarData.movementState,
        .bodyMotion = radarData.bodyMovement,   
        .bed = (BedStatus)(isPresent ? BED_STATUS_IN : BED_STATUS_AWAY),
        .sleepStatus = (SleepStatus)radarData.sleepState
    };
    
    char jsonBuffer[350];
    int len = _protocol.buildPresenceJson(&presence, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "存在信息");
}

/**
 * @brief 发送呼吸心率信息
 * @param radarData 雷达数据
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendBreathHeartInfo(const RadarR60A::RadarData& radarData) {
    BreathHeartInfo info = {
        .heartRate = radarData.heartRate,
        .respiration = radarData.breathRate,
        .respirationStatus = (BreathStatus)radarData.breathStatus
    };
    
    char jsonBuffer[300];
    int len = _protocol.buildBreathHeartJson(&info, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "呼吸心率");
}

/**
 * @brief 发送睡眠综合状态
 * @param radarData 雷达数据
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendSleepSummary(const RadarR60A::RadarData& radarData) {
    ProtocolSleepSummary summary = {
        .exist = radarData.isDetected,
        .sleepStatus = (SleepStatus)radarData.sleepState,
        .meanSleepRespiration = radarData.breathRate,
        .sleepMeanHeartbeat = radarData.heartRate,
        .numberTurns = 0,
        .maxBodyMotion = radarData.bodyMovement,
        .minBodyMotion = 0,
        .respirationStopNum = 0
    };
    
    char jsonBuffer[400];
    int len = _protocol.buildSleepSummaryJson(&summary, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "睡眠综合");
}

/**
 * @brief 发送异常挣扎信息
 * @param state 异常状态
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendAbnormalInfo(AbnormalStatus state) {
    char jsonBuffer[250];
    int len = _protocol.buildAbnormalJson(state, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "异常挣扎");
}

/**
 * @brief 发送无人计时状态
 * @param state 无人计时状态
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendNoPersonInfo(AbnormalStatus state) {
    char jsonBuffer[250];
    int len = _protocol.buildNoPersonJson(state, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "无人计时");
}

/**
 * @brief 发送睡眠报告
 * @param report 睡眠报告
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendSleepReport(const SleepReport& report) {
    char jsonBuffer[450];
    int len = _protocol.buildSleepReportJson(&report, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "睡眠报告");
}

/**
 * @brief 发送指令应答
 * @param cmdID 指令ID
 * @param result 执行结果
 * @return true-发送成功，false-发送失败
 */
bool DataSender::sendCommandResponse(CommandID cmdID, CommandResult result) {
    char jsonBuffer[256];
    int len = _protocol.buildCommandResponseJson(cmdID, result, jsonBuffer, sizeof(jsonBuffer));
    return sendJson(jsonBuffer, len, "指令应答");
}