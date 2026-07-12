/**
 * @file debug.h
 * @brief 结构化调试日志宏 — AI 可解析的固定格式
 *
 * 输出格式： [LEVEL][MODULE] key=value ...\n
 * 
 * 用法：
 *   LOG(OK,   "boot", "init_complete uptime=%lu", g_uptime);
 *   LOG(DBG,  "adc",  "value=%d channel=3", val);
 *   LOG(WARN, "mem",  "heap_used=%d limit=%d", used, limit);
 *   LOG(ERR,  "uart", "timeout device=%d", dev);
 *   LOG(FATAL,"sys",  "hardfault addr=0x%08X", addr);
 *
 * 日志级别 (LEVEL)：
 *   OK    — 预期行为确认（如初始化完成、任务成功）
 *   DBG   — 调试信息（如变量值、中间状态）
 *   WARN  — 警告（非预期但可恢复）
 *   ERR   — 错误（操作失败但系统继续运行）
 *   FATAL — 致命错误（输出后停止执行，while(1)死锁）
 *
 * MODULE 为自由文本（如 "led", "uart", "adc", "timer"），
 * 用于标识日志来源模块。
 *
 * 所有 key=value 均为纯文本，无 ANSI 转义码，便于 grep/正则解析。
 *
 * AI 解析正则： ^\[(OK|DBG|WARN|ERR|FATAL)\]\[(\w+)\]\s+(.+)$
 *   捕获组：#1=级别, #2=模块名, #3=消息体(key=value ...)
 */

#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdio.h>
#include <stdint.h>

/* 日志级别枚举 */
typedef enum {
    LOG_OK   = 0,
    LOG_DBG  = 1,
    LOG_WARN = 2,
    LOG_ERR  = 3,
    LOG_FATAL= 4
} debug_level_t;

/* 级别名映射 */
static const char * const g_level_names[] = {
    "OK", "DBG", "WARN", "ERR", "FATAL"
};

/* ---------------------------------------------------------------- */
/* 核心宏：LOG(级别, 模块, 格式化字符串, ...)                        */
/* ---------------------------------------------------------------- */
#define LOG(level, module, fmt, ...)                                    \
    do {                                                                \
        printf("[%s][%s] " fmt "\n",                                    \
               g_level_names[LOG_##level], module, ##__VA_ARGS__);      \
        if (LOG_##level == LOG_FATAL) {                                 \
            while(1) {}                                                 \
        }                                                               \
    } while(0)

#endif /* __DEBUG_H */
