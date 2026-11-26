#include <jni.h>
extern "C" void bench_trace_frame_end(void);

extern "C" JNIEXPORT void JNICALL Java_BenchMain_frameEnd(JNIEnv *,jclass){bench_trace_frame_end();}
