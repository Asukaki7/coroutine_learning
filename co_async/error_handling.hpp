#pragma once

#include <cerrno>
#include <system_error>
#if !defined(NDEBUG)
#include <source_location>
#endif
#include <vector>


namespace co_async {

    /// @brief  把c语言errno 转换为C++ 异常-1是因为linux系统对于-1都有各种错误
    /// @param res
    /// @return
#if !defined(NDEBUG)
    auto checkError(auto res, std::source_location const& loc = std::source_location::current()) {
        if (res == -1) [[unlikely]] {
            throw std::system_error(errno, std::system_category(),
                (std::string)loc.file_name() + ":" + std::to_string(loc.line()));
        }
        return res;
    }
#else 
    auto checkError(auto res) {
        if (res == -1) [[unlikely]] {
            throw std::system_error(errno, std::system_category());
        }
        return res;
    }
#endif

}





