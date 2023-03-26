LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
$(warning "su")


LIBEVNET_SOURCES := main.cpp pts.cpp su_client.cpp  su_service.cpp

LOCAL_MODULE    := su
LOCAL_SRC_FILES := $(LIBEVNET_SOURCES)


include $(BUILD_EXECUTABLE)