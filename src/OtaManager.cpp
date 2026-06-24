#include "OtaManager.h"

OtaManager::OtaManager() 
    : _status(OTA_STATUS_IDLE), _progress(0) {
    strcpy(_currentVersion, "1.0.0");
}

bool OtaManager::startUpdate(const char* url, const char* version, OtaProgressCallback callback) {
    if (_status != OTA_STATUS_IDLE) {
        Serial0.println("[OTA] 错误：OTA 升级正在进行中");
        return false;
    }
    
    if (!url || strlen(url) == 0) {
        Serial0.println("[OTA] 错误：OTA URL 为空");
        return false;
    }
    
    _callback = callback;
    _status = OTA_STATUS_DOWNLOADING;
    _progress = 0;
    
    Serial0.print("[OTA] 开始 OTA 升级，版本：");
    Serial0.println(version);
    Serial0.print("[OTA] 固件 URL：");
    Serial0.println(url);
    
    notifyCallback(OTA_STATUS_DOWNLOADING, 0, "开始下载固件");
    
    bool success = downloadAndWrite(url);
    
    if (success) {
        _status = OTA_STATUS_SUCCESS;
        _progress = 100;
        notifyCallback(OTA_STATUS_SUCCESS, 100, "OTA 升级成功，准备重启");
        Serial0.println("[OTA] OTA 升级成功，准备重启设备");
        
        delay(3000);
        ESP.restart();
    } else {
        _status = OTA_STATUS_FAILED;
        notifyCallback(OTA_STATUS_FAILED, _progress, "OTA 升级失败");
        Serial0.println("[OTA] OTA 升级失败");
    }
    
    return success;
}

OtaStatus OtaManager::getStatus() const {
    return _status;
}

int OtaManager::getProgress() const {
    return _progress;
}

const char* OtaManager::getCurrentVersion() const {
    return _currentVersion;
}

bool OtaManager::hasNewVersion(const char* currentVersion, const char* newVersion) {
    if (!currentVersion || !newVersion) return false;
    
    int v1[3] = {0}, v2[3] = {0};
    sscanf(currentVersion, "%d.%d.%d", &v1[0], &v1[1], &v1[2]);
    sscanf(newVersion, "%d.%d.%d", &v2[0], &v2[1], &v2[2]);
    
    for (int i = 0; i < 3; i++) {
        if (v2[i] > v1[i]) return true;
        if (v2[i] < v1[i]) return false;
    }
    return false;
}

void OtaManager::update() {
    // OTA 升级是同步执行的，不需要在 loop 中处理
}

void OtaManager::reset() {
    _status = OTA_STATUS_IDLE;
    _progress = 0;
}

bool OtaManager::downloadAndWrite(const char* url) {
    _httpClient.begin(url);
    _httpClient.setTimeout(30000);
    
    int httpCode = _httpClient.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        Serial0.print("[OTA] HTTP 请求失败，错误码：");
        Serial0.println(httpCode);
        _httpClient.end();
        return false;
    }
    
    int contentLength = _httpClient.getSize();
    Serial0.print("[OTA] 固件大小：");
    Serial0.print(contentLength);
    Serial0.println(" 字节");
    
    if (contentLength <= 0 || contentLength > 4 * 1024 * 1024) {
        Serial0.println("[OTA] 错误：固件大小异常");
        _httpClient.end();
        return false;
    }
    
    if (!Update.begin(contentLength)) {
        Serial0.println("[OTA] 错误：OTA 更新初始化失败");
        _httpClient.end();
        return false;
    }
    
    _status = OTA_STATUS_WRITING;
    notifyCallback(OTA_STATUS_WRITING, 0, "开始写入固件");
    
    WiFiClient* stream = _httpClient.getStreamPtr();
    uint8_t buffer[4096];
    int totalBytes = 0;
    int lastProgress = 0;
    
    while (stream->available() > 0) {
        int bytesRead = stream->readBytes(buffer, sizeof(buffer));
        
        if (bytesRead > 0) {
            size_t written = Update.write(buffer, bytesRead);
            
            if (written != bytesRead) {
                Serial0.println("[OTA] 错误：写入 Flash 失败");
                Update.abort();
                _httpClient.end();
                return false;
            }
            
            totalBytes += bytesRead;
            int progress = (totalBytes * 100) / contentLength;
            
            if (progress - lastProgress >= 10) {
                _progress = progress;
                lastProgress = progress;
                notifyCallback(OTA_STATUS_WRITING, progress, "写入固件中");
                Serial0.print("[OTA] 写入进度：");
                Serial0.print(progress);
                Serial0.println("%");
            }
        }
    }
    
    _httpClient.end();
    
    if (Update.end(true)) {
        Serial0.println("[OTA] 固件写入完成");
        return true;
    } else {
        Serial0.print("[OTA] 错误：OTA 更新失败：");
        Serial0.println(Update.errorString());
        return false;
    }
}

void OtaManager::notifyCallback(OtaStatus status, int progress, const char* message) {
    if (_callback) {
        _callback(status, progress, message);
    }
}
