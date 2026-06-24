/**
 * @brief 简单的 HTTP 文件服务器，用于 OTA 固件下载
 * @details 该服务器用于托管 ESP32 固件文件，支持 HTTP 下载
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = 8080;
const HOST = '0.0.0.0';  // 监听所有网络接口

// MIME 类型映射
const mimeTypes = {
    '.bin': 'application/octet-stream',
    '.txt': 'text/plain',
    '.json': 'application/json',
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.css': 'text/css',
    '.md': 'text/markdown'
};

/**
 * @brief 创建 HTTP 服务器
 */
const server = http.createServer((req, res) => {
    console.log(`[${new Date().toISOString()}] ${req.method} ${req.url}`);

    // 处理根路径，显示文件列表
    if (req.url === '/' || req.url === '/list') {
        const files = fs.readdirSync('.');
        let html = '<!DOCTYPE html><html><head><meta charset="UTF-8"><title>OTA 服务器</title></head><body>';
        html += '<h1>OTA 固件服务器</h1>';
        html += '<h2>可用文件：</h2><ul>';
        for (const file of files) {
            const stats = fs.statSync(file);
            if (stats.isFile()) {
                html += `<li><a href="/${file}">${file}</a> (${stats.size} 字节)</li>`;
            }
        }
        html += '</ul></body></html>';
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(html);
        return;
    }

    // 处理文件下载
    const filePath = '.' + req.url;
    const extname = path.extname(filePath).toLowerCase();
    const contentType = mimeTypes[extname] || 'application/octet-stream';

    fs.readFile(filePath, (err, content) => {
        if (err) {
            if (err.code === 'ENOENT') {
                res.writeHead(404, { 'Content-Type': 'text/plain' });
                res.end('404 Not Found: ' + filePath);
            } else {
                res.writeHead(500, { 'Content-Type': 'text/plain' });
                res.end('500 Internal Server Error: ' + err.code);
            }
        } else {
            res.writeHead(200, {
                'Content-Type': contentType,
                'Content-Length': content.length,
                'Content-Disposition': 'attachment'  // 提示下载
            });
            res.end(content);
        }
    });
});

server.listen(PORT, HOST, () => {
    console.log('========================================');
    console.log('  OTA 固件服务器已启动');
    console.log('========================================');
    console.log(`监听地址: http://${HOST}:${PORT}`);
    console.log(`本地访问: http://localhost:${PORT}`);
    console.log(`局域网访问: http://YOUR_IP:${PORT}/firmware.bin`);
    console.log('========================================');
    console.log('将固件文件放入此目录即可通过 HTTP 下载');
    console.log('按 Ctrl+C 停止服务器');
    console.log('========================================');
});
