#pragma once
#include <spdlog/spdlog.h>

template <typename... Args>
static inline void
Ensure_(bool cond, int line, const char* file, const char* exp,
        spdlog::format_string_t<Args...> msg = "no info provided",
        Args&&... args)
{
    if (cond)
        return;

    spdlog::critical("[{}:{}] ensure failed '{}'", file, line, exp);
    spdlog::critical(msg, std::forward<Args>(args)...);
    abort();
}

#define Ensure(cond, ...)                                                      \
    Ensure_((cond), __LINE__, __FILE__, #cond, ##__VA_ARGS__)
