#ifndef BENCH_ANDROID_LOG_H
#define BENCH_ANDROID_LOG_H
#include <stdarg.h>
#include <stdio.h>
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
static inline int __android_log_vprint(int priority,const char *tag,const char *fmt,va_list args){(void)priority;(void)tag;return vfprintf(stderr,fmt,args);}
static inline int __android_log_print(int priority,const char *tag,const char *fmt,...){va_list args;va_start(args,fmt);int result=__android_log_vprint(priority,tag,fmt,args);va_end(args);return result;}
#endif
