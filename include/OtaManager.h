#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>

/**
 * @brief OTA 升级状态枚举
 */
typedef enum {
    OTA_STATUS_IDLE = 0,        // 空闲
    OTA_STATUS_DOWNLOADING,     // 下载中
    OTA_STATUS_WRITING,         // 写入中
    OTA_STATUS_SUCCESS,         // 成功
    OTA_STATUS_FAILED           // 失败
} OtaStatus;

/**
 * @brief OTA 升级回调函数类型
 */
typedef void (*OtaProgressCallback)(OtaStatus status, int progress, const char* message);

/**
 * @brief OTA 升级管理器
 */
class OtaManager {
public:
    /**
     * @brief 构造函数
     */
    OtaManager();
    
    /**
     * @brief 开始 OTA 升级
     * @param url 固件下载 URL
     * @param version 新版本号
     * @param callback 进度回调函数
     * @return true-开始升级，false-升级失败
     */
    bool startUpdate(const char* url, const char* version, OtaProgressCallback callback = nullptr);
    
    /**
     * @brief 获取当前升级状态
     * @return OTA 状态
     */
    OtaStatus getStatus() const;
    
    /**
     * @brief 获取升级进度（0-100）
     * @return 进度百分比
     */
    int getProgress() const;
    
    /**
     * @brief 获取当前固件版本
     * @return 版本字符串
     */
    const char* getCurrentVersion() const;
    
    /**
     * @brief 检查是否有新版本
     * @param currentVersion 当前版本
     * @param newVersion 新版本
     * @return true-有新版本，false-无新版本
     */
    bool hasNewVersion(const char* currentVersion, const char* newVersion);
    
    /**
     * @brief 在 loop 中调用，处理 OTA 升级
     */
    void update();
    
    /**
     * @brief 重置 OTA 状态
     */
    void reset();

private:
    OtaStatus _status;
    int _progress;
    char _currentVersion[16];
    OtaProgressCallback _callback;
    HTTPClient _httpClient;
    
    /**
     * @brief 下载固件并写入 Flash
     * @param url 固件 URL
     * @return true-成功，false-失败
     */
    bool downloadAndWrite(const char* url);
    
    /**
     * @brief 调用回调函数
     * @param status OTA 状态
     * @param progress 进度
     * @param message 消息
     */
    void notifyCallback(OtaStatus status, int progress, const char* message);
};

#endif // OTA_MANAGER_H
