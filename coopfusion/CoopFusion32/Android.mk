LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := CoopFusion32
LOCAL_CPP_EXTENSION := .cpp
LOCAL_SRC_FILES := \
    coopfusion/runtime/main.cpp \
    coopfusion/runtime.cpp \
    coopfusion/ui/ui_common.cpp \
    coopfusion/ui/fx.cpp \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/coopfusion/transport/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/coopfusion/world/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/coopfusion/diagnostics/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/coopfusion/platform/mod/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/imgui/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/imgui/backends/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/imgui/RW/*.cpp))

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/coopfusion \
    $(LOCAL_PATH)/coopfusion/platform \
    $(LOCAL_PATH)/coopfusion/platform/mod \
    $(LOCAL_PATH)/coopfusion/platform/aml-psdk \
    $(LOCAL_PATH)/coopfusion/transport \
    $(LOCAL_PATH)/coopfusion/world \
    $(LOCAL_PATH)/coopfusion/ui \
    $(LOCAL_PATH)/imgui \
    $(LOCAL_PATH)/imgui/RW

LOCAL_CPPFLAGS += -std=c++17 -DAML32 -DCOOPFUSION32 -DDONT_USE_STB -fexceptions -frtti -Wno-format-security
LOCAL_CFLAGS += -DAML32
LOCAL_LDLIBS += -llog -landroid -lz -latomic -lEGL -lGLESv1_CM -lGLESv2

include $(BUILD_SHARED_LIBRARY)
