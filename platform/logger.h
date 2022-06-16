/* Copyright (c) 2021-2021, Xuanyi Technologies
 *
 * Features:
 *      Define Logger Macros as Android style
 *
 * Version:
 *      2021.02.10  initial
 *
 */

#pragma once

#include <iostream>
#include <spdlog/fmt/fmt.h>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#define LOGGER_FORMAT "[%^%l%$] %v"
#define PROJECT_NAME "glTFViewer"

#define __FILENAME__ (static_cast<const char *>(__FILE__) + ROOT_PATH_SIZE)

#define initSpdlog()   do {                                                             \
    try {                                                                               \
        auto logger = spdlog::basic_logger_mt("basic_logger", "logmessage.log");        \
        spdlog::set_pattern("[%H:%M:%S.%e] %L [thread %t] %v");                         \
        spdlog::set_default_logger(logger);                                             \
        spdlog::flush_on(spdlog::level::err);                                           \
        spdlog::flush_every(std::chrono::seconds(3));                                   \
        spdlog::info("=============NEW START=============");                            \
    } catch (const spdlog::spdlog_ex &ex) {                                             \
        std::cout << "Log init failed: " << ex.what() << std::endl;                     \
    }                                                                                   \
} while (0)

#define spdlogLevelTrace()    spdlog::set_level(spdlog::level::trace)
#define spdlogLevelDebug()    spdlog::set_level(spdlog::level::debug)
#define spdlogLevelInfo()     spdlog::set_level(spdlog::level::info)
#define spdlogLevelWarn()     spdlog::set_level(spdlog::level::warn)
#define spdlogLevelError()    spdlog::set_level(spdlog::level::err)

#define TRACE()     spdlog::trace("{}: == {} ==", __FILENAME__, __func__)
#define LOGD(...)   spdlog::debug(__VA_ARGS__)
#define LOGI(...)   spdlog::info(__VA_ARGS__)
#define LOGW(...)   spdlog::warn(__VA_ARGS__)
#define LOGE(...)   spdlog::error("{}:{} {}", __FILENAME__, __LINE__, fmt::format(__VA_ARGS__))

#define shutdownSpdlog()  do {                                                          \
    spdlog::drop_all();                                                                 \
    spdlog::shutdown();                                                                 \
} while (0)
