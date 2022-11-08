LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
$(warning "su")

LOCAL_MODULE    := root
LOCAL_SRC_FILES := main.cpp pts.cpp su_client.cpp  su_service.cpp

include $(BUILD_EXECUTABLE)