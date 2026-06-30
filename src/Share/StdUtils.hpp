/*!
 * \file StdUtils.hpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief C++标准库一些定义的简单封装,方便调用
 */
#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdint.h>
#include <string>
#include <filesystem>
#include <fstream>

//////////////////////////////////////////////////////////////////////////
//std线程类
typedef std::thread StdThread;
typedef std::shared_ptr<StdThread> StdThreadPtr;

//////////////////////////////////////////////////////////////////////////
//std互斥量和锁
typedef std::recursive_mutex	StdRecurMutex;
typedef std::mutex				StdUniqueMutex;
typedef std::condition_variable_any	StdCondVariable;

typedef std::unique_lock<StdUniqueMutex>	StdUniqueLock;

template<typename T>
class StdLocker
{
public:
	StdLocker(T& mtx)
	{
		mtx.lock();
		_mtx = &mtx;
	}

	~StdLocker(){
		_mtx->unlock();
	}

private:
	T* _mtx;
};

//////////////////////////////////////////////////////////////////////////
//文件辅助类
class StdFile
{
public:
    static inline uint64_t read_file_content(const char* filename, std::string& content, std::size_t offset = 0, std::size_t length = 0)
    {
        // 检查文件是否存在
        if (!std::filesystem::exists(filename)) 
		{
            throw std::runtime_error("File does not exist");
        }
        
        // 获取文件大小
        auto file_size = std::filesystem::file_size(filename);
        
        // 检查偏移量是否有效
        if (offset > file_size) 
		{
            throw std::runtime_error("Offset exceeds file size");
        }
        
        // 如果未指定长度，则读取从offset到文件末尾的所有内容
        if (length == 0) 
		{
            length = static_cast<std::size_t>(file_size - offset);
        }
        
        // 确保读取长度不超过文件边界
        if (offset + length > file_size) 
		{
            length = static_cast<std::size_t>(file_size - offset);
        }
        
        // 使用文件流读取
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) 
		{
            throw std::runtime_error("Failed to open file");
        }
        
        // 定位到指定偏移位置
        file.seekg(static_cast<std::streamoff>(offset));
        
        // 调整缓冲区大小
        content.resize(length);
        
        // 读取数据
        if (length > 0) 
		{
            file.read(&content[0], static_cast<std::streamsize>(length));
            
            // 检查是否读取成功
            if (file.gcount() != static_cast<std::streamsize>(length)) 
			{
                throw std::runtime_error("Failed to read expected number of bytes");
            }
        }
        
        file.close();
        return static_cast<uint64_t>(length);
    }

    static inline void write_file_content(const char* filename, const std::string& content)
    {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) 
		{
            throw std::runtime_error("Failed to open file for writing");
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.close();
    }

    static inline void write_file_content(const char* filename, const void* data, std::size_t length)
    {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) 
		{
            throw std::runtime_error("Failed to open file for writing");
        }
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(length));
        file.close();
    }

    static inline bool exists(const char* filename)
    {
        try 
		{
            return std::filesystem::exists(filename);
        } catch (...) 
		{
            return false;
        }
    }
};