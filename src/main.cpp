// dantes_inferno - ReXGlue Recompiled Project

#include "generated/default/dantes_inferno_init.h"

#include "dantes_inferno_app.h"
#include "dantes_iso_installer.h"

#if defined(__ANDROID__)
#include <jni.h>
#include <cstdlib>

extern "C" JNIEXPORT void JNICALL
Java_com_dantesinferno_game_MainActivity_setGameRootEnv(JNIEnv* env, jobject /* thiz */, jstring path) {
  if (!path) return;
  const char* native_str = env->GetStringUTFChars(path, nullptr);
  if (native_str) {
    setenv("DANTES_GAME_ROOT", native_str, 1);
    REXLOG_INFO("JNI: setenv DANTES_GAME_ROOT={}", native_str);
    env->ReleaseStringUTFChars(path, native_str);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_dantesinferno_game_MainActivity_nativeOnIsoPicked(JNIEnv* env, jobject /* thiz */, jstring path) {
  if (!path) {
    dantes::android::SetPendingIsoPath("");
    return;
  }
  const char* native_str = env->GetStringUTFChars(path, nullptr);
  if (native_str) {
    setenv("DANTES_INSTALL_ISO", native_str, 1);
    dantes::android::SetPendingIsoPath(native_str);
    REXLOG_INFO("JNI: setenv DANTES_INSTALL_ISO={}", native_str);
    env->ReleaseStringUTFChars(path, native_str);
  }
}
#endif

REX_DEFINE_APP(dantes_inferno, DantesInfernoApp::Create)

