#include <jni.h>
#include "AmbilightCapture.h" // Generated header from javac -h

extern "C" {
    // From your existing code
    struct PixelResult {
        int* pixels;
        int size;
    };
    PixelResult getScreenPixels(const char* screenIdStr, int ledX, int ledY, int keepPixels, float reduction);
    void freeMemory(const char* ptr);

    JNIEXPORT jobject JNICALL Java_AmbilightCapture_getScreenPixels(JNIEnv* env, jobject obj,
        jstring screenId, jint ledX, jint ledY, jint keepPixels, jfloat reduction) {
        const char* screenIdStr = env->GetStringUTFChars(screenId, nullptr);
        PixelResult result = getScreenPixels(screenIdStr, ledX, ledY, keepPixels, reduction);
        env->ReleaseStringUTFChars(screenId, screenIdStr);

        // Create PixelResult object
        jclass pixelResultClass = env->FindClass("AmbilightCapture$PixelResult");
        jmethodID constructor = env->GetMethodID(pixelResultClass, "<init>", "(JI)V");
        jobject pixelResultObj = env->NewObject(pixelResultClass, constructor,
            (jlong)result.pixels, (jint)result.size);

        return pixelResultObj;
    }

    JNIEXPORT void JNICALL Java_AmbilightCapture_freeMemory(JNIEnv* env, jobject obj, jlong ptr) {
        freeMemory(reinterpret_cast<const char*>(ptr));
    }

    JNIEXPORT jintArray JNICALL Java_AmbilightCapture_getPixelArray(JNIEnv* env, jobject obj,
        jlong pixels, jint size) {
        int* pixelData = reinterpret_cast<int*>(pixels);
        jintArray result = env->NewIntArray(size);
        if (result == nullptr) return nullptr;

        env->SetIntArrayRegion(result, 0, size, pixelData);
        return result;
    }
}