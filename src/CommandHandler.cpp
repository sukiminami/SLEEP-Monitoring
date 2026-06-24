/**
 * @file CommandHandler.cpp
 * @brief 命令处理模块实现
 * @details 实现解析和执行来自服务器的下行指令的功能
 */

#include "CommandHandler.h"

/**
 * @brief 构造函数
 * @param protocol 协议模块引用
 * @param dataSender 数据发送器引用
 * @param radar 雷达模块引用
 * @param params 设备参数指针
 */
CommandHandler::CommandHandler(Protocol& protocol, DataSender& dataSender, RadarR60A& radar, DeviceParams* params, OtaManager& otaManager)
    : _protocol(protocol), _dataSender(dataSender), _radar(radar), _deviceParams(params), _otaManager(otaManager) {
}

/**
 * @brief 处理接收到的命令数据
 * @param data 数据指针
 * @param length 数据长度
 */
void CommandHandler::handleCommand(const uint8_t* data, uint16_t length) {
    Serial0.print("[命令处理] 收到下行指令，长度：");
    Serial0.println(length);
    
    const int MAX_BUFFER_SIZE = 512;
    char jsonBuffer[MAX_BUFFER_SIZE];
    
    // 保留一个字节给字符串结束符 '\0'
    if (length >= MAX_BUFFER_SIZE - 1) {
        Serial0.println("[命令处理] 指令数据过长，丢弃");
        return;
    }
    
    memcpy(jsonBuffer, data, length);
    jsonBuffer[length] = '\0';
    
    Serial0.print("[命令处理] 指令内容：");
    Serial0.println(jsonBuffer);
    
    DownlinkCommand command;
    if (!_protocol.parseDownlinkCommand(jsonBuffer, &command)) {
        Serial0.println("[命令处理] 解析指令失败");
        return;
    }
    
    Serial0.print("[命令处理] 解析成功，cmdID: ");
    Serial0.println(command.cmdID);
    
    CommandResult result = CMD_RESULT_ERROR;
    
    switch (command.cmdID) {
        case CMD_ID_SET_PARAMS:
            result = executeSetParams(command);
            break;
        case CMD_ID_RESET_MODULE:
            result = executeResetModule();
            break;
        case CMD_ID_REBOOT_DEVICE:
            result = executeRebootDevice();
            break;
        case CMD_ID_OTA_UPDATE:
            result = executeOtaUpdate(command);
            break;
        default:
            Serial0.println("[命令处理] 未知指令ID");
            result = CMD_RESULT_ERROR;
            break;
    }
    
    _dataSender.sendCommandResponse(command.cmdID, result);
    
    if (command.cmdID == CMD_ID_REBOOT_DEVICE && result == CMD_RESULT_SUCCESS) {
        delay(1000);
        ESP.restart();
    }
}

/**
 * @brief 执行设置参数命令
 * @param command 命令结构体
 * @return 执行结果
 * @details 根据接收到的参数配置向雷达模块发送相应的控制命令，包括：
 *          - 人体存在检测开关
 *          - 呼吸检测开关
 *          - 心率检测开关
 *          - 睡眠监测开关
 */
CommandResult CommandHandler::executeSetParams(const DownlinkCommand& command) {
    Serial0.println("[命令处理] 执行设置参数指令");
    
    if (_deviceParams == nullptr) {
        Serial0.println("[命令处理] 参数更新失败：参数指针为空");
        return CMD_RESULT_ERROR;
    }
    
    // 向雷达模块发送控制命令（只处理被设置的参数）
    bool allSuccess = true;
    bool anyUpdated = false;
    
    // 处理人体存在检测开关
    if (command.paramsMask.existSwitch) {
        anyUpdated = true;
        _deviceParams->existSwitch = command.params.existSwitch;
        if (_deviceParams->existSwitch) {
            Serial0.println("[命令处理] 开启人体存在检测");
            if (!_radar.enableHumanPresence()) {
                allSuccess = false;
            }
        } else {
            Serial0.println("[命令处理] 关闭人体存在检测");
            if (!_radar.disableHumanPresence()) {
                allSuccess = false;
            }
        }
    }
    
    // 处理呼吸检测开关
    if (command.paramsMask.breathingSwitch) {
        anyUpdated = true;
        _deviceParams->breathingSwitch = command.params.breathingSwitch;
        if (_deviceParams->breathingSwitch) {
            Serial0.println("[命令处理] 开启呼吸检测");
            if (!_radar.enableBreathDetection()) {
                allSuccess = false;
            }
        } else {
            Serial0.println("[命令处理] 关闭呼吸检测");
            if (!_radar.disableBreathDetection()) {
                allSuccess = false;
            }
        }
    }
    
    // 处理心率检测开关
    if (command.paramsMask.heartRateSwitch) {
        anyUpdated = true;
        _deviceParams->heartRateSwitch = command.params.heartRateSwitch;
        if (_deviceParams->heartRateSwitch) {
            Serial0.println("[命令处理] 开启心率检测");
            if (!_radar.enableHeartRateDetection()) {
                allSuccess = false;
            }
        } else {
            Serial0.println("[命令处理] 关闭心率检测");
            if (!_radar.disableHeartRateDetection()) {
                allSuccess = false;
            }
        }
    }
    
    // 处理睡眠监测开关
    if (command.paramsMask.sleepSwitch) {
        anyUpdated = true;
        _deviceParams->sleepSwitch = command.params.sleepSwitch;
        if (_deviceParams->sleepSwitch) {
            Serial0.println("[命令处理] 开启睡眠监测");
            if (!_radar.enableSleepMonitoring()) {
                allSuccess = false;
            }
        } else {
            Serial0.println("[命令处理] 关闭睡眠监测");
            if (!_radar.disableSleepMonitoring()) {
                allSuccess = false;
            }
        }
    }
    
    // 处理异常挣扎检测开关
    if (command.paramsMask.abnormalStruggleSwitch) {
        anyUpdated = true;
        _deviceParams->abnormalStruggleSwitch = command.params.abnormalStruggleSwitch;
        if (_deviceParams->abnormalStruggleSwitch) {
            Serial0.println("[命令处理] 开启异常挣扎检测");
            if (!_radar.enableAbnormalStruggleDetection(true)) {
                allSuccess = false;
            }
        } else {
            Serial0.println("[命令处理] 关闭异常挣扎检测");
            if (!_radar.enableAbnormalStruggleDetection(false)) {
                allSuccess = false;
            }
        }
    }
    
    // 处理无人计时开关
    if (command.paramsMask.longTimeNoTimerSwitch) {
        anyUpdated = true;
        _deviceParams->longTimeNoTimerSwitch = command.params.longTimeNoTimerSwitch;
        if (_deviceParams->longTimeNoTimerSwitch) {
            Serial0.println("[命令处理] 开启无人计时功能");
            if (!_radar.enableLongTimeNoTimer(true)) {
                allSuccess = false;
            }
        } else {
            Serial0.println("[命令处理] 关闭无人计时功能");
            if (!_radar.enableLongTimeNoTimer(false)) {
                allSuccess = false;
            }
        }
    }
    
    // 处理无人计时时长
    if (command.paramsMask.unmanneDuration) {
        anyUpdated = true;
        _deviceParams->unmanneDuration = command.params.unmanneDuration;
        Serial0.print("[命令处理] 设置无人计时时长: ");
        Serial0.println(_deviceParams->unmanneDuration);
        if (!_radar.setLongTimeNoTimerDuration(_deviceParams->unmanneDuration)) {
            allSuccess = false;
        }
    }
    
    // 处理探测模式
    if (command.paramsMask.detectionMode) {
        anyUpdated = true;
        _deviceParams->detectionMode = command.params.detectionMode;
        Serial0.print("[命令处理] 设置探测模式: ");
        Serial0.println(_deviceParams->detectionMode == DETECT_MODE_REAL_TIME ? "实时模式" : "睡眠模式");
        if (!_radar.setDetectionMode(_deviceParams->detectionMode)) {
            allSuccess = false;
        }
    }
    
    if (!anyUpdated) {
        Serial0.println("[命令处理] 未收到任何有效参数");
        return CMD_RESULT_ERROR;
    }
    
    if (allSuccess) {
        Serial0.println("[命令处理] 参数更新成功");
        return CMD_RESULT_SUCCESS;
    } else {
        Serial0.println("[命令处理] 参数部分更新成功，部分雷达控制命令发送失败");
        return CMD_RESULT_ERROR;
    }
}

/**
 * @brief 执行复位模组命令
 * @return 执行结果
 */
CommandResult CommandHandler::executeResetModule() {
    Serial0.println("[命令处理] 执行复位模组指令");
    _radar.softResetModule();
    Serial0.println("[命令处理] 模组复位完成");
    return CMD_RESULT_SUCCESS;
}

/**
 * @brief 执行重启设备命令
 * @return 执行结果
 */
CommandResult CommandHandler::executeRebootDevice() {
    Serial0.println("[命令处理] 执行重启设备指令");
    return CMD_RESULT_SUCCESS;
}

/**
 * @brief 执行OTA升级命令
 * @param command 命令结构体
 * @return 执行结果
 */
CommandResult CommandHandler::executeOtaUpdate(const DownlinkCommand& command) {
    Serial0.println("[命令处理] OTA升级指令");
    
    if (strlen(command.otaUrl) == 0) {
        Serial0.println("[命令处理] 错误：OTA URL 为空");
        return CMD_RESULT_ERROR;
    }
    
    Serial0.print("[命令处理] 当前版本：");
    Serial0.println(command.versionOld);
    Serial0.print("[命令处理] 目标版本：");
    Serial0.println(command.versionNew);
    Serial0.print("[命令处理] OTA URL：");
    Serial0.println(command.otaUrl);
    
    if (!_otaManager.hasNewVersion(command.versionOld, command.versionNew)) {
        Serial0.println("[命令处理] 警告：目标版本不高于当前版本");
    }
    
    bool success = _otaManager.startUpdate(command.otaUrl, command.versionNew);
    
    if (success) {
        Serial0.println("[命令处理] OTA 升级已启动");
        return CMD_RESULT_SUCCESS;
    } else {
        Serial0.println("[命令处理] OTA 升级启动失败");
        return CMD_RESULT_ERROR;
    }
}