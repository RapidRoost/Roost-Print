#include "slic3r/GUI/FileUploader.hpp"
// 生成随机的multipart边界
std::string generate_boundary() {
    static const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string boundary;
    
    // 生成一个20字符的随机边界
    for (int i = 0; i < 20; ++i) {
        boundary += chars[rand() % chars.size()];
    }
    
    return "---------------------------" + boundary;
}

// 获取文件大小
std::streamsize get_file_size(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("can't open file: " + filename);
    }
    return file.tellg();
}
HttpFileUploader::HttpFileUploader(asio::io_context& io_context,
                    const std::string& server,
                    const std::string& port,
                    const std::string& target,
                    const std::string& filename,
                    const std::string& field_name)
        : resolver_(io_context),
          socket_(io_context),
          connect_timer_(io_context),
          send_timer_(io_context),
          server_(server),
          target_(target),
          filename_(filename),
          field_name_(field_name),
          boundary_(generate_boundary()),
          io_context_(io_context),
          is_canceled_(false),
          is_completed_(false),
          progress_callback_(nullptr),
          success_callback_(nullptr),
          error_callback_(nullptr),
          cancel_callback_(nullptr),
          connect_timeout_(5000),  // 默认连接超时5秒
          send_timeout_(60000)     // 默认发送超时30秒
        {
        // 打开文件
        file_.open(filename, std::ios::binary);
        if (!file_.is_open()) {
            throw std::runtime_error("无法打开文件: " + filename);
        }
        
        // 获取文件大小
        file_size_ = get_file_size(filename);
    }