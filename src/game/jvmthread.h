#pragma once
// 5.02.65: JVM-attached worker threads.
//
// Crash log (realme RE6054, Android 16, 32-bit, 70 AML mods, MP 5.02.64): SIGABRT in libc abort(), and the abort
// message on the stack: "Binder thread started or Java binder used, but env null. Attach JVM?". That is
// LOG_ALWAYS_FATAL in Android's android_util_Binder.cpp: a binder call came in for a Java object (a Java
// callback / listener in the game's process) on a thread that has no JNIEnv. Binder delivers a nested
// callback on the thread that is waiting for the outgoing call - so a plain native thread that talks to a
// system service (OpenSL ES / audio players being made or destroyed, the media stream player, the file
// picker / clipboard calls) can get a Java callback and Android kills the game.
//
// Every MP worker thread that touches audio, media or Java now runs attached to the JVM for its whole
// life (and detaches when it ends, so ART never sees a native thread exit while still attached).
// Threads that already have a JNIEnv (the game's threads, the UI thread) are left alone.
#include <jni.h>
#include <pthread.h>
#include <mod/amlmod.h>

namespace MPJvm
{
    struct ThreadScope
    {
        JavaVM* vm = nullptr;
        JNIEnv* env = nullptr;
        bool    attached = false;
        explicit ThreadScope(const char* name = "MP worker")
        {
            vm = aml ? aml->GetJavaVM() : nullptr;
            if(!vm) return;
            if(vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) return;   // already attached
            JavaVMAttachArgs a = { JNI_VERSION_1_6, name, nullptr };
            if(vm->AttachCurrentThread(&env, &a) == JNI_OK && env) attached = true;
            else env = nullptr;
        }
        ~ThreadScope() { if(attached && vm) vm->DetachCurrentThread(); }
        ThreadScope(const ThreadScope&) = delete;
        ThreadScope& operator=(const ThreadScope&) = delete;
    };
}
