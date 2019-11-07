#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <jni.h>

extern int main();

/*
 * JNI init
 */

JavaVM* javaVM;
JNIEnv* jniEnv;
jclass  activityClz;
jobject activityObj;
jmethodID outputTextId;

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved)
{
    if ((*vm)->GetEnv(vm, (void**)&jniEnv, JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR; // JNI version not supported.
    }
    javaVM = vm;
    return  JNI_VERSION_1_6;
}

void callOutputText(const char* text) {
    JNIEnv *env = jniEnv;

    jstring javaMsg = (*env)->NewStringUTF(env, text);
    (*env)->CallVoidMethod(env, activityObj, outputTextId, javaMsg);
    (*env)->DeleteLocalRef(env, javaMsg);
}

/*
 * Override printf, puts, putchar
 */

int printf(const char * format, ... )
{
    char buff[256] = {};

    va_list args;
    va_start (args, format);
    const int result = vsnprintf(buff, sizeof(buff), format, args);
    va_end (args);

    if (result > 0) {
        callOutputText(buff);
    }
    return result;
}

int puts(const char *s)
{
    callOutputText(s);
    callOutputText("\n");
    return strlen(s);
}

int putchar(int c)
{
    char buff[2] = { c, '\0' };
    callOutputText(buff);
    return c;
}

void* runner(void* context) {
    (*javaVM)->AttachCurrentThread(javaVM, &jniEnv, NULL);
    main();
    return NULL;
}

JNIEXPORT void JNICALL
Java_com_example_wasm3_MainActivity_runMain(JNIEnv* env, jobject instance)
{
    jclass clz = (*env)->GetObjectClass(env, instance);
    activityClz = (*env)->NewGlobalRef(env, clz);
    activityObj = (*env)->NewGlobalRef(env, instance);

    outputTextId = (*env)->GetMethodID(env, activityClz,
                                       "outputText",
                                       "(Ljava/lang/String;)V");

    pthread_attr_t  threadAttr;
    pthread_attr_init(&threadAttr);
    pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);

    pthread_t       threadInfo;
    pthread_create( &threadInfo, &threadAttr, runner, NULL);

    pthread_attr_destroy(&threadAttr);
}
