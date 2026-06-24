/**
 * @file CommandHandler.h
 * @brief 命令处理模块接口
 * @details 负责解析和执行来自服务器的下行指令
 */

#ifndef COMMANDHANDLER_H
#define COMMANDHANDLER_H

#include "Protocol.h"
#include "DataSender.h"
#include "RadarR60A.h"
#include "OtaManager.h"

class CommandHandler {
public:
    /**
     * @brief 构造函数
     * @param protocol 协议模块引用
     * @param dataSender 数据发送器引用
     * @param radar 雷达模块引用
     * @param params 设备参数指针
     * @param otaManager OTA管理器引用
     */
    CommandHandler(Protocol& protocol, DataSender& dataSender, RadarR60A& radar, DeviceParams* params, OtaManager& otaManager);
    
    /**
     * @brief 处理接收到的命令数据
     * @param data 数据指针
     * @param length 数据长度
     */
    void handleCommand(const uint8_t* data, uint16_t length);

private:
    Protocol& _protocol;
    DataSender& _dataSender;
    RadarR60A& _radar;
    DeviceParams* _deviceParams;
    OtaManager& _otaManager;
    
    /**
     * @brief 执行设置参数命令
     * @param command 命令结构体
     * @return 执行结果
     */
    CommandResult executeSetParams(const DownlinkCommand& command);
    
    /**
     * @brief 执行复位模组命令
     * @return 执行结果
     */
    CommandResult executeResetModule();
    
    /**
     * @brief 执行重启设备命令
     * @return 执行结果
     */
    CommandResult executeRebootDevice();
    
    /**
     * @brief 执行OTA升级命令
     * @param command 命令结构体
     * @return 执行结果
     */
    CommandResult executeOtaUpdate(const DownlinkCommand& command);
};

#endif // COMMANDHANDLER_H