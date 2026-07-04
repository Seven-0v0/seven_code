/**
 * @file syscalls.c
 * @brief 系统调用实现（用于 newlib）
 */

#include <sys/stat.h>
#include <errno.h>

// 未实现的系统调用，使用弱符号定义

__attribute__((weak)) int _close(int file)
{
    (void)file;
    return -1;
}

__attribute__((weak)) int _lseek(int file, int ptr, int dir)
{
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

__attribute__((weak)) int _read(int file, char *ptr, int len)
{
    (void)file;
    (void)ptr;
    (void)len;
    return 0;
}

__attribute__((weak)) int _fstat(int file, struct stat *st)
{
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

__attribute__((weak)) int _isatty(int file)
{
    (void)file;
    return 1;
}

__attribute__((weak)) void *_sbrk(int incr)
{
    extern char _heap_start; // 链接脚本中定义
    extern char _heap_end;
    static char *heap_ptr = &_heap_start;
    char *prev_heap_ptr = heap_ptr;

    if (heap_ptr + incr > &_heap_end) {
        errno = ENOMEM;
        return (void *)-1;
    }

    heap_ptr += incr;
    return (void *)prev_heap_ptr;
}

__attribute__((weak)) int _getpid(void)
{
    return 1;
}

__attribute__((weak)) int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}
