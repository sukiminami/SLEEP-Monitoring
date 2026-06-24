/**
 * @file DataSender.h
 * @brief 数据发送模块接口
 * @details 负责将雷达数据按照SM-C0x协议规范打包并发送到服务器
 */

#ifndef DATASENDER_H
#define DATASENDER_H

#include "Protocol.h"
#include "NetworkManager.h"
#include "RadarR60A.h"

class DataSender {
public:
    /**
     * @brief 构造函数
     * @param protocol 协议模块引用
     * @param networkManager 网络管理器引用
     */
    DataSender(Protocol& protocol, NetworkManager& networkManager);
    
    /**
     * @brief 发送心跳信息
     * @return true-发送成功，false-发送失败
     */
    bool sendHeartbeat();
    
    /**
     * @brief 发送设备参数信息
     * @param params 设备参数
     * @return true-发送成功，false-发送失败
     */
    bool sendDeviceParams(const DeviceParams& params);
    
    /**
     * @brief 发送存在信息
     * @param radarData 雷达数据
     * @return true-发送成功，false-发送失败
     */
    bool sendPresenceInfo(const RadarR60A::RadarData& radarData);
    
    /**
     * @brief 发送呼吸心率信息
     * @param radarData 雷达数据
     * @return true-发送成功，false-发送失败
     */
    bool sendBreathHeartInfo(const RadarR60A::RadarData& radarData);
    
    /**
     * @brief 发送睡眠综合状态
     * @param radarData 雷达数据
     * @return true-发送成功，false-发送失败
     */
    bool sendSleepSummary(const RadarR60A::RadarData& radarData);
    
    /**
     * @brief 发送异常挣扎信息
     * @param state 异常状态
     * @return true-发送成功，false-发送失败
     */
    bool sendAbnormalInfo(AbnormalStatus state);
    
    /**
     * @brief 发送无人计时状态
     * @param state 无人计时状态
     * @return true-发送成功，false-发送失败
     */
    bool sendNoPersonInfo(AbnormalStatus state);
    
    /**
     * @brief 发送睡眠报告
     * @param report 睡眠报告
     * @return true-发送成功，false-发送失败
     */
    bool sendSleepReport(const SleepReport& report);
    
    /**
     * @brief 发送指令应答
     * @param cmdID 指令ID
     * @param result 执行结果
     * @return true-发送成功，false-发送失败
     */
    bool sendCommandResponse(CommandID cmdID, CommandResult result);
    
    /**
     * @brief 检查网络是否就绪
     * @return true-网络就绪，false-网络未就绪
     */
    bool isNetworkReady();

private:
    Protocol& _protocol;
    NetworkManager& _networkManager;
    
    /**
     * @brief 发送JSON数据到网络
     * @param jsonData JSON数据
     * @param length 数据长度
     * @param tag 日志标签
     * @return true-发送成功，false-发送失败
     */
    bool sendJson(const char* jsonData, int length, const char* tag);
};

#endif // DATASENDER_H