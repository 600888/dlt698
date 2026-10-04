/**
 * @file test.hpp
 * @brief 测试断言、十六进制输入及测试入口辅助函数。
 */
#pragma once
#include <dlt698/common/bytes.hpp>
#include <iostream>
#include <stdexcept>

/**
 * @brief 检查断言并在失败时生成带行号的诊断。
 * @param[in] condition 断言结果。
 * @param[in] expression 用于诊断的表达式文本。
 * @param[in] line 断言所在的源码行号。
 * @throws std::runtime_error 断言失败。
 */
inline void check(bool condition, const char* expression, int line) {
    if (!condition)
        throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}

#define CHECK(...) check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)

/**
 * @brief 将测试中的十六进制文本转换为字节序列。
 * @param[in] input 以空字符结尾的十六进制文本。
 * @return 拥有内存的测试字节序列。
 * @throws std::runtime_error 测试文本无法解析。
 */
inline dlt698::Bytes hex(const char* input) {
    auto result = dlt698::from_hex(input);
    CHECK(result);
    return std::move(result).value();
}

/**
 * @brief 执行测试入口并将标准异常转换为失败退出码。
 * @tparam F 无参数测试入口的可调用类型。
 * @param[in] run 待执行的测试入口。
 * @return 成功返回 0，捕获标准异常时返回 1。
 */
template <class F>
int tests(F run) {
    try {
        run();
        std::cout << "All checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
