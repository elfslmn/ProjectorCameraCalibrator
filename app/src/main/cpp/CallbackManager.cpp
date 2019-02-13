//
// Created by esalman17 on 5.10.2018.
//

#include "CallbackManager.h"
#include "Util.h"

CallbackManager::CallbackManager(){}

CallbackManager::CallbackManager(JavaVM* vm, jobject& obj, jmethodID& amplitudeCallbackID, jmethodID& shapeDetectedCallbackID){
    m_vm = vm;
    m_obj = obj;
    m_amplitudeCallbackID = amplitudeCallbackID;
    m_shapeDetectedCallbackID = shapeDetectedCallbackID;
}

void CallbackManager::sendImageToJavaSide(const cv::Mat& image)
{
    jint fill[image.rows * image.cols];
    if(image.type() == 16) // CV_8UC3
    {
        //  int color = (A & 0xff) << 24 | (R & 0xff) << 16 | (G & 0xff) << 8 | (B & 0xff);
        int k = 0;
        for (int i = 0; i < image.rows; i++)
        {
            const cv::Vec3b *ptr = image.ptr<cv::Vec3b>(i);
            for (int j = 0; j < image.cols; j++, k++)
            {
                cv::Vec3b p = ptr[j];
                int color = (255 & 0xff) << 24 | (p[2] & 0xff) << 16 | (p[1] & 0xff) << 8 | (p[0] & 0xff);
                fill[k] = color;
            }
        }
    }
    else if(image.type() <= 6) // 1 channel images
    {
        cv::Mat norm;
        normalize(image, norm, 0, 255, cv::NORM_MINMAX, CV_8UC1);
        int k = 0;
        for (int i = 0; i < norm.rows; i++)
        {
            auto *ptr = norm.ptr<uint8_t>(i);
            for (int j = 0; j < norm.cols; j++, k++)
            {
                uint8_t p = ptr[j];
                int color = (255 & 0xff) << 24 | (p & 0xff) << 16 | (p & 0xff) << 8 | (p & 0xff);
                fill[k] = color;
            }
        }
    }
    else{
        LOGE("Image should have 1 channel or CV_8UC3");
        return;
    }

    // attach to the JavaVM thread and get a JNI interface pointer
    JNIEnv *env;
    m_vm->AttachCurrentThread(&env, NULL);
    jintArray intArray = env->NewIntArray(image.rows * image.cols);
    env->SetIntArrayRegion(intArray, 0, image.rows * image.cols, fill);
    env->CallVoidMethod(m_obj, m_amplitudeCallbackID, intArray);
    m_vm->DetachCurrentThread();
}

void CallbackManager::onShapeDetected(const std::vector<int> & arr){
    JNIEnv *env;
    m_vm->AttachCurrentThread(&env, NULL);
    jintArray intArray = env->NewIntArray(arr.size());
    env->SetIntArrayRegion(intArray, 0, arr.size(), &arr[0]);
    env->CallVoidMethod(m_obj, m_shapeDetectedCallbackID, intArray);
    m_vm->DetachCurrentThread();
}
