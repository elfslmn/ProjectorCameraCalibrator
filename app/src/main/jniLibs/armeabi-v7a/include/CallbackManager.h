//
// Created by esalman17 on 5.10.2018.
//

#include <jni.h>
#include <opencv2/core.hpp>

class CallbackManager {

public:
    CallbackManager();
    CallbackManager(JavaVM* vm, jobject& obj, jmethodID& amplitudeCallbackID, jmethodID& shapeDetectedCallbackID);

    // It sends whole image to java
    void sendImageToJavaSide(const cv::Mat& image);

    // It sends center of the detected retro as array
    void onShapeDetected(const std::vector<int> & arr);

private:
    JavaVM* m_vm;
    jmethodID m_amplitudeCallbackID;
    jmethodID m_shapeDetectedCallbackID;
    jobject m_obj;
};

