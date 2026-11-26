#ifndef BENCH_ANDROID_BITMAP_H
#define BENCH_ANDROID_BITMAP_H
#include <jni.h>
static inline int AndroidBitmap_lockPixels(JNIEnv *env,jobject bitmap,void **pixels){
    jclass cls=env->GetObjectClass(bitmap);
    jfieldID field=env->GetFieldID(cls,"pixels","Ljava/nio/ByteBuffer;");
    jobject buffer=env->GetObjectField(bitmap,field);
    *pixels=env->GetDirectBufferAddress(buffer);
    env->DeleteLocalRef(buffer);env->DeleteLocalRef(cls);
    return *pixels?0:-1;
}
static inline int AndroidBitmap_unlockPixels(JNIEnv *env,jobject bitmap){(void)env;(void)bitmap;return 0;}
#endif
