#ifndef slic3r_GUI_HttpFileUploader_hpp_
#define slic3r_GUI_HttpFileUploader_hpp_
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
namespace asio = boost::asio;
using boost::asio::ip::tcp;
namespace posix_time = boost::posix_time;


// HTTP文件上传类
class HttpFileUploader {
public:
    using ProgressCallback = std::function<void(double progress, std::streamsize bytes_sent, std::streamsize total_size)>;
    // 定义成功回调函数类型
    // 参数: 服务器响应内容、文件名称
    using SuccessCallback = std::function<void(const std::string& response, const std::string& filename)>;
    
    // 定义错误回调函数类型
    // 参数: 错误信息、错误代码(可选)
    using ErrorCallback = std::function<void(const std::string& error_msg, int error_code)>;
    // 定义取消回调函数类型
    using CancelCallback = std::function<void(const std::string& filename)>;
    HttpFileUploader(asio::io_context& io_context,
                    const std::string& server,
                    const std::string& port,
                    const std::string& target,
                    const std::string& filename,
                    const std::string& field_name = "file");

    // 开始上传
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_ || is_completed_) return;
        
        // 启动连接超时计时器
        start_connect_timer();
        // 解析服务器地址
        resolver_.async_resolve(server_, "http",
            boost::bind(&HttpFileUploader::on_resolve, this,
                        asio::placeholders::error,
                        asio::placeholders::results));
    }

    // 设置进度回调函数
    void set_progress_callback(ProgressCallback callback) {
        progress_callback_ = std::move(callback);
    }
    // 设置成功回调函数
    void set_success_callback(SuccessCallback callback) {
        success_callback_ = std::move(callback);
    }
    
    // 设置错误回调函数
    void set_error_callback(ErrorCallback callback) {
        error_callback_ = std::move(callback);
    }
    // 设置取消回调函数
    void set_cancel_callback(CancelCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        cancel_callback_ = std::move(callback);
    }
  // 取消上传操作
    void cancel() {
        asio::post(io_context_, [this]() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_canceled_ || is_completed_) return;
            
            // 标记为已取消
            is_canceled_ = true;
            
            // 关闭socket以取消所有异步操作
            boost::system::error_code ec;
            socket_.close(ec);
            
            // 关闭文件
            if (file_.is_open()) {
                file_.close();
            }
            
            // 触发取消回调
            if (cancel_callback_) {
                cancel_callback_("");
            } else {
                std::cout << "file upload canceled: " << std::endl;
            }
            
            // 停止IO上下文
            io_context_.stop();
        });
    }
    
    // 检查是否已取消
    bool is_canceled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_canceled_;
    }
    
    // 检查是否已完成
    bool is_completed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_completed_;
    }
private:
    void start_connect_timer() {
        connect_timer_.expires_from_now(posix_time::milliseconds(connect_timeout_));
        connect_timer_.async_wait(boost::bind(&HttpFileUploader::on_connect_timeout, this,
            asio::placeholders::error));
    }
    
    // 启动发送超时计时器
    void start_send_timer() {
        send_timer_.expires_from_now(posix_time::milliseconds(send_timeout_));
        send_timer_.async_wait(boost::bind(&HttpFileUploader::on_send_timeout, this,
            asio::placeholders::error));
    }
    
    // 取消所有计时器
    void cancel_timers() {
        boost::system::error_code ec;
        connect_timer_.cancel(ec);
        send_timer_.cancel(ec);
    }
     // 连接超时回调
    void on_connect_timeout(const boost::system::error_code& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (err || is_canceled_ || is_completed_ || socket_.is_open()) {
            return;
        }
        
        // 连接超时
        handle_error("connect timeout(" + std::to_string(connect_timeout_) + "ms)", 1001);
    }
    
    // 发送超时回调
    void on_send_timeout(const boost::system::error_code& err) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (err || is_canceled_ || is_completed_) {
            return;
        }
        
        // 发送超时
        handle_error("send timeout(" + std::to_string(send_timeout_) + "ms)", 1002);
    }
    // 解析地址完成回调
    void on_resolve(const boost::system::error_code& err,
                   const tcp::resolver::results_type& endpoints) {
        if (err) {
            cancel_timers();
            std::string error_msg = "resolve error: " + err.message();
            handle_error(error_msg, 5);
            return;
        }

        // 连接到服务器
        asio::async_connect(socket_, endpoints,
            boost::bind(&HttpFileUploader::on_connect, this,
                        asio::placeholders::error));
    }

    // 连接完成回调
    void on_connect(const boost::system::error_code& err) {
        if (is_canceled_) return;
        
        // 取消连接计时器，启动发送计时器
        connect_timer_.cancel();
        start_send_timer();
        if (err) {
            std::string error_msg = "connect error: " + err.message();
            handle_error(error_msg, 7);
            return;
        }

        // 构建HTTP请求头
        build_request_header();
        
        // 发送HTTP请求头
        asio::async_write(socket_, request_header_,
            boost::bind(&HttpFileUploader::on_header_written, this,
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
    }

    // 构建HTTP请求头
    void build_request_header() {
        // 计算总内容长度
        std::string filename = filename_.substr(filename_.find_last_of("/\\") + 1);
        
        // 计算multipart头部和尾部的大小
        std::stringstream header_part;
        header_part << "--" << boundary_ << "\r\n";
        header_part << "Content-Disposition: form-data; name=\"" << field_name_ << "\"; filename=\"" << filename << "\"\r\n";
        header_part << "Content-Type: application/octet-stream\r\n\r\n";
        
        std::string footer_part = "\r\n--" + boundary_ + "--\r\n";
        
        // 总内容长度
        // 总内容长度（用于进度计算）
        total_content_length_ = header_part.str().size() + file_size_ + footer_part.size();
        
        // 构建HTTP请求
        std::ostream request_stream(&request_header_);
        request_stream << "POST " << target_ << " HTTP/1.1\r\n";
        request_stream << "Host: " << server_ << "\r\n";
        request_stream << "User-Agent: Boost.Asio File Uploader\r\n";
        request_stream << "Content-Type: multipart/form-data; boundary=" << boundary_ << "\r\n";
        request_stream << "Content-Length: " << total_content_length_ << "\r\n";
        request_stream << "Connection: close\r\n\r\n";
        request_stream << header_part.str();
        
        // 保存footer供后续发送
        footer_ = footer_part;
        header_part_ = header_part.str();
    }

    // 请求头发送完成回调
    void on_header_written(const boost::system::error_code& err,
                          std::size_t bytes_transferred) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "send http header error: " + err.message();
            handle_error(error_msg, 19);
            return;
        }

        // 发送文件内容
        send_file_chunk();
    }

    // 发送文件分片
    void send_file_chunk() {
        if (is_canceled_) return;
        // 读取文件分片
        file_.read(buffer_, max_chunk_size);
        std::streamsize bytes_read = file_.gcount();
        
        if (bytes_read > 0) {
            // 发送当前分片
            asio::async_write(socket_, asio::buffer(buffer_, bytes_read),
                boost::bind(&HttpFileUploader::on_file_chunk_sent, this,
                            asio::placeholders::error,
                            asio::placeholders::bytes_transferred,
                            bytes_read));
        } else {
            // 文件发送完成，发送footer
            send_footer();
        }
    }

    // 文件分片发送完成回调
    void on_file_chunk_sent(const boost::system::error_code& err,
                           std::size_t bytes_transferred,
                           std::streamsize bytes_read) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "send file error: " + err.message();
            handle_error(error_msg, 20);
            return;
        }

       // 更新已发送字节数
        total_sent_ += bytes_transferred;
        
        // 触发进度回调
        trigger_progress_callback();

        // 继续发送下一个分片
        send_file_chunk();
    }

    // 发送multipart footer
    void send_footer() {
        if (is_canceled_) return;
        asio::async_write(socket_, asio::buffer(footer_),
            boost::bind(&HttpFileUploader::on_footer_sent, this,
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
    }

    // Footer发送完成回调
    void on_footer_sent(const boost::system::error_code& err,
                       std::size_t bytes_transferred) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "send footer error: " + err.message();
            handle_error(error_msg, 23);
            return;
        }
        // 更新已发送字节数（包括footer）
        total_sent_ += bytes_transferred;
        
        // 触发最终进度回调（100%）
        trigger_progress_callback();
        

        // 读取服务器响应
        asio::async_read_until(socket_, response_, "\r\n\r\n",
            boost::bind(&HttpFileUploader::on_response_header_received, this,
                        asio::placeholders::error,
                        asio::placeholders::bytes_transferred));
    }
      // 重置发送计时器（在有进度更新时调用）
    void reset_send_timer() {
        // 取消当前计时器并重新启动
        boost::system::error_code ec;
        send_timer_.cancel(ec);
        start_send_timer();
    }
     // 触发进度回调
    void trigger_progress_callback() {
        reset_send_timer();
        if (progress_callback_ && total_content_length_ > 0) {
            double progress = static_cast<double>(total_sent_) / total_content_length_ * 100.0;
            progress_callback_(progress, total_sent_, total_content_length_);
        }
    }

    // 接收响应头回调
    void on_response_header_received(const boost::system::error_code& err,
                                    std::size_t bytes_transferred) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        if (err) {
            std::string error_msg = "receive reponse: " + err.message();            
            // 触发错误回调
            handle_error(error_msg, err.value());
            return;
        }

        // 解析响应头
        std::istream response_stream(&response_);
        std::string http_version;
        response_stream >> http_version;
        unsigned int status_code;
        response_stream >> status_code;
        std::string status_message;
        std::getline(response_stream, status_message);

        // 读取响应头的其余部分
        std::string header;
        while (std::getline(response_stream, header) && header != "\r") {}

        // 读取响应体
        std::string response_body;
        if (response_.size() > 0) {
            std::stringstream ss;
            ss << &response_;
            response_body = ss.str();
        }

        // 检查是否还有更多响应体数据
        if (status_code == 200) {
            // 读取完整响应体
            asio::async_read(socket_, response_,
                asio::transfer_all(),
                boost::bind(&HttpFileUploader::on_response_body_complete, this,
                            asio::placeholders::error,
                            asio::placeholders::bytes_transferred,
                            status_code,
                            response_body));
        } else {
            // 非200状态码，处理错误
            std::string error_msg = "Upload failed: " + std::to_string(status_code) + "msg: " + status_message;
            // 触发错误回调
            handle_error(error_msg, status_code);

        }
    }
    
    // 响应体完全接收回调
    void on_response_body_complete(const boost::system::error_code& err,
                                  std::size_t bytes_transferred,
                                  unsigned int status_code,
                                  std::string response_body) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_canceled_) return;
        
        // 取消所有计时器
        cancel_timers();
        // 合并已读取的响应体
        if (bytes_transferred > 0) {
            std::stringstream ss;
            ss << &response_;
            response_body += ss.str();
        }
        
        if (status_code == 200) {
            std::cout << "Upload sucess!" << std::endl;
            is_completed_ = true;
            // 触发成功回调
            if (success_callback_) {
                success_callback_(response_body, "");
            }
        } else {
            std::string error_msg = "Upload failed: " + std::to_string(status_code);            
            // 触发错误回调
            handle_error(error_msg, status_code);

        }
        
        io_context_.stop();
    }
    // 处理错误
    void handle_error(const std::string& error_msg, int error_code) {
        if (is_canceled_) return;
        
        std::cerr << error_msg << std::endl;
        is_completed_ = true;
        
        // 取消所有计时器
        cancel_timers();
        
        // 触发错误回调
        if (error_callback_) {
            error_callback_(error_msg, error_code);
        }
        
        // 关闭socket
        boost::system::error_code ec;
        socket_.close(ec);
        
        // 关闭文件
        if (file_.is_open()) {
            file_.close();
        }
        
        io_context_.stop();
    }

    tcp::resolver resolver_;
    tcp::socket socket_;
    asio::deadline_timer connect_timer_;  // 连接超时计时器
    asio::deadline_timer send_timer_;     // 发送超时计时器
    asio::streambuf request_header_;
    asio::streambuf response_;
    std::ifstream file_;
    std::string server_;
    std::string target_;
    std::string filename_;
    std::string field_name_;
    std::string boundary_;
    std::string header_part_;
    std::string footer_;
    std::streamsize file_size_ = 0;
    std::streamsize total_sent_ = 0;
    asio::io_context& io_context_;  // 引用IO上下文，用于控制退出
    ProgressCallback progress_callback_;  
    SuccessCallback success_callback_;    // 成功回调函数
    ErrorCallback error_callback_;        // 错误回调函数 
    CancelCallback cancel_callback_;      // 取消回调函数
    std::streamsize total_content_length_ = 0; 
    mutable std::mutex mutex_;            // 线程安全互斥锁
    bool is_canceled_;                    // 取消状态标记
    bool is_completed_;
    int connect_timeout_;                 // 连接超时时间(毫秒)
    int send_timeout_;                    // 发送超时时间(毫秒)
    // 分片大小设置为8KB
    enum { max_chunk_size = 65536 };
    char buffer_[max_chunk_size];
};
#endif