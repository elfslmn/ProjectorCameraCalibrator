#include <royale/CameraManager.hpp>
#include <royale/ICameraDevice.hpp>
#include <iostream>
#include <jni.h>
#include <thread>
#include <chrono>
#include <mutex>
#include "opencv2/opencv.hpp"
#include <util.h>
#include <Calibrator.h>

#ifdef __cplusplus
extern "C"
{
#endif

using namespace royale;
using namespace std;
using namespace cv;

// this represents the main camera device object
static std::unique_ptr<ICameraDevice> cameraDevice;
Calibrator calibrator; // It is a child of IDepthDataListener

jintArray Java_com_esalman17_calibrator_MainActivity_OpenCameraNative (JNIEnv *env, jobject thiz, jint fd, jint vid, jint pid)
{
    LOGD("OpenCameraNative()");
    // the camera manager will query for a connected camera
    {
        CameraManager manager;
        auto camlist = manager.getConnectedCameraList (fd, vid, pid);
        LOGI ("Detected %zu camera(s).", camlist.size());

        if (!camlist.empty())
        {
            cameraDevice = manager.createCamera (camlist.at (0));
        }
    }
    // the camera device is now available and CameraManager can be deallocated here

    if (cameraDevice == nullptr)
    {
        LOGE ("Cannot create the camera device");
    }

    // IMPORTANT: call the initialize method before working with the camera device
    CameraStatus ret = cameraDevice->initialize();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Cannot initialize the camera device, CODE %d", (int) ret);
    }

    royale::Vector<royale::String> opModes;
    royale::String cameraName;
    royale::String cameraId;

    ret = cameraDevice->getUseCases (opModes);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get use cases, CODE %d", (int) ret);
    }

    uint16_t cam_width, cam_height;
    ret = cameraDevice->getMaxSensorWidth (cam_width);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get max sensor width, CODE %d", (int) ret);
    }

    ret = cameraDevice->getMaxSensorHeight (cam_height);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get max sensor height, CODE %d", (int) ret);
    }

    ret = cameraDevice->getId (cameraId);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get camera ID, CODE %d", (int) ret);
    }

    ret = cameraDevice->getCameraName (cameraName);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get camera name, CODE %d", (int) ret);
    }

    // display some information about the connected camera
    LOGI ("====================================");
    LOGI ("        Camera information");
    LOGI ("====================================");
    LOGI ("Id:              %s", cameraId.c_str());
    LOGI ("Type:            %s", cameraName.c_str());
    LOGI ("Width:           %d", cam_width);
    LOGI ("Height:          %d", cam_height);
    LOGI ("Operation modes: %zu", opModes.size());

    for (int i = 0; i < opModes.size(); i++)
    {
        LOGI ("    %s", opModes.at (i).c_str());
    }

    LensParameters lensParams;
    ret = cameraDevice->getLensParameters (lensParams);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get lens parameters, CODE %d", (int) ret);
    }else{
        calibrator.setLensParameters (lensParams);
    }

    // Set camera and projector values for calibration
    calibrator.setCamera(cam_width, cam_height, 62, 45);
    calibrator.setProjector(1280, 720, 46.4, 24.2);    // TODO make it generic

    // register a data listener
    ret = cameraDevice->registerDataListener (&calibrator);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to register data listener, CODE %d", (int) ret);
    }

    // set an operation mode
    ret = cameraDevice->setUseCase (opModes[0]);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to set use case, CODE %d", (int) ret);
    }

    //set exposure mode to manual
    ret = cameraDevice->setExposureMode (ExposureMode::MANUAL);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to set exposure mode, CODE %d", (int) ret);
    }

    //set exposure time (not working above 300)
    ret = cameraDevice->setExposureTime(30);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to set exposure time, CODE %d", (int) ret);
    }

    jint fill[2];
    fill[0] = cam_width;
    fill[1] = cam_height;

    jintArray intArray = env->NewIntArray (2);
    env->SetIntArrayRegion (intArray, 0, 2, fill);

    return intArray;
}

void Java_com_esalman17_calibrator_MainActivity_RegisterCallback (JNIEnv *env, jobject thiz)
{
    LOGD("RegisterCallback()");
    // save JavaVM globally; needed later to call Java method in the listener
    JavaVM* m_vm;
    env->GetJavaVM (&m_vm);

    jobject m_obj = env->NewGlobalRef (thiz);

    // save refs for callback
    jclass g_class = env->GetObjectClass (m_obj);
    if (g_class == NULL)
    {
        std::cout << "Failed to find class" << std::endl;
    }

    // save method ID to call the method later in the listener
    jmethodID m_amplitudeCallbackID = env->GetMethodID (g_class, "amplitudeCallback", "([I)V");
    jmethodID m_blobsCallbackID = env->GetMethodID (g_class, "blobsCallback", "([I)V");

    calibrator.callbackManager = CallbackManager(m_vm, m_obj, m_amplitudeCallbackID, m_blobsCallbackID);
}

jboolean Java_com_esalman17_calibrator_MainActivity_StartCaptureNative (JNIEnv *env, jobject thiz)
{
    LOGD("StartCaptureNative()");
    if (cameraDevice == nullptr)
    {
        LOGE("There is no camera device to  start");
        return (jboolean)false;
    }
    auto ret = cameraDevice->startCapture();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE("Failed to start capture, CODE %d", (int) ret);
        return (jboolean)false;
    }
    LOGI("Capture started.");
    return (jboolean)true;
}

jboolean Java_com_esalman17_calibrator_MainActivity_StopCaptureNative (JNIEnv *env, jobject thiz)
{
    LOGD("StopCaptureNative()");
    if (cameraDevice == nullptr)
    {
        LOGE ("There is no camera device to  stop");
        return (jboolean)false;
    }
    auto ret = cameraDevice->stopCapture();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to stop capture, CODE %d", (int) ret);
        return (jboolean)false;
    }
    LOGI("Capture stopped.");
    return (jboolean)true;
}

void Java_com_esalman17_calibrator_MainActivity_ChangeModeNative (JNIEnv *env, jobject thiz, jint mode)
{
    calibrator.setMode(mode); // TODO chcek the setmode method, java and cpp side can be different
}
jboolean Java_com_esalman17_calibrator_MainActivity_AddPointNative (JNIEnv *env, jobject thiz)
{
    return (jboolean)calibrator.saveCamPoint();
}

void Java_com_esalman17_calibrator_MainActivity_CalibrateNative (JNIEnv *env, jobject thiz)
{
    calibrator.calibrate();
}

#ifdef __cplusplus
}
#endif
