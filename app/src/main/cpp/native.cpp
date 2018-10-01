#include <royale/CameraManager.hpp>
#include <royale/ICameraDevice.hpp>
#include <iostream>
#include <jni.h>
#include <android/log.h>
#include <thread>
#include <chrono>
#include <mutex>
#include "opencv2/opencv.hpp"
#include <Shape.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "Native", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "Native", __VA_ARGS__))

using namespace royale;
using namespace std;
using namespace cv;

JavaVM *m_vm;
jmethodID m_amplitudeCallbackID;
jobject m_obj;

uint16_t width, height;

// this represents the main camera device object
static std::unique_ptr<ICameraDevice> cameraDevice;

class MyListener : public IDepthDataListener
{
    Mat cameraMatrix, distortionCoefficients;
    Mat zImage, grayImage;

    mutex flagMutex;
    Mat drawing, norm;

    const float MAX_DISTANCE = 1.0f;

    void transferImageToJavaSide(Mat& image)
    {
        jint fill[image.rows * image.cols];
        if(image.type() == 16) // CV_8UC3
        {
            //  int color = (A & 0xff) << 24 | (R & 0xff) << 16 | (G & 0xff) << 8 | (B & 0xff);
            int k = 0;
            for (int i = 0; i < image.rows; i++)
            {
                Vec3b *ptr = image.ptr<Vec3b>(i);
                for (int j = 0; j < image.cols; j++, k++)
                {
                    Vec3b p = ptr[j];
                    int color = (255 & 0xff) << 24 | (p[2] & 0xff) << 16 | (p[1] & 0xff) << 8 | (p[0] & 0xff);
                    fill[k] = color;
                }
            }
        }
        else if(image.type() <= 6) // 1 channel images
        {
            normalize(image, norm, 0, 255, NORM_MINMAX, CV_8UC1);
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
        m_vm->AttachCurrentThread((JNIEnv **) &env, NULL);
        jintArray intArray = env->NewIntArray(image.rows * image.cols);
        env->SetIntArrayRegion(intArray, 0, image.rows * image.cols, fill);
        env->CallVoidMethod(m_obj, m_amplitudeCallbackID, intArray);
        m_vm->DetachCurrentThread();
    }

    void getDepthImage(const DepthData* data, Mat& image ){
        int k = image.rows * image.cols -1 ; // to reverse scrren
        for (int y = 0; y < image.rows; y++)
        {
            float *zRowPtr = image.ptr<float> (y);
            for (int x = 0; x < image.cols; x++, k--)
            {
                auto curPoint = data->points.at (k);
                if (curPoint.depthConfidence > 0)
                {
                    zRowPtr[x] = curPoint.z < MAX_DISTANCE ? curPoint.z : MAX_DISTANCE; //clip
                }
                else
                {
                    zRowPtr[x] = 0;
                }
            }
        }
    }

    void getGrayImage(const DepthData* data, Mat& image ){
        int k = image.rows * image.cols -1 ; // to reverse scrren
        for (int y = 0; y < image.rows; y++)
        {
            uint16_t *gRowPtr = grayImage.ptr<uint16_t> (y);
            for (int x = 0; x < image.cols; x++, k--)
            {
                auto curPoint = data->points.at (k);
                if (curPoint.depthConfidence > 0)
                {
                    gRowPtr[x] = curPoint.grayValue;
                }
                else
                {
                    gRowPtr[x] = 0;
                }
            }
        }
    }

    void onNewData (const DepthData *data)
    {
        lock_guard<mutex> lock (flagMutex);

        //getDepthImage(data, zImage);
        //transferImageToJavaSide(zImage);

        getGrayImage(data, grayImage);
        transferImageToJavaSide(grayImage);
    }

public :
    void setLensParameters (LensParameters lensParameters)
    {
        // Construct the camera matrix
        // (fx   0    cx)
        // (0    fy   cy)
        // (0    0    1 )
        cameraMatrix = (Mat1d (3, 3) << lensParameters.focalLength.first, 0, lensParameters.principalPoint.first,
                0, lensParameters.focalLength.second, lensParameters.principalPoint.second,
                0, 0, 1);
        LOGI("Camera params fx fy cx cy: %f,%f,%f,%f", lensParameters.focalLength.first, lensParameters.focalLength.second,
             lensParameters.principalPoint.first, lensParameters.principalPoint.second);

        // Construct the distortion coefficients
        // k1 k2 p1 p2 k3
        distortionCoefficients = (Mat1d (1, 5) << lensParameters.distortionRadial[0],
                lensParameters.distortionRadial[1],
                lensParameters.distortionTangential.first,
                lensParameters.distortionTangential.second,
                lensParameters.distortionRadial[2]);
        LOGI("Dist coeffs k1 k2 p1 p2 k3 : %f,%f,%f,%f,%f", lensParameters.distortionRadial[0],
             lensParameters.distortionRadial[1],
             lensParameters.distortionTangential.first,
             lensParameters.distortionTangential.second,
             lensParameters.distortionRadial[2]);
    }

    void initialize(){
        //zImage.create (Size (width,height), CV_32FC1);
        grayImage.create (Size (width,height), CV_16UC1);
        //drawing = Mat::zeros(height, width, CV_8UC3);
    }

};

MyListener listener;

jintArray Java_com_esalman17_calibrator_MainActivity_OpenCameraNative (JNIEnv *env, jobject thiz, jint fd, jint vid, jint pid)
{
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

    ret = cameraDevice->getMaxSensorWidth (width);
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to get max sensor width, CODE %d", (int) ret);
    }

    ret = cameraDevice->getMaxSensorHeight (height);
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
    LOGI ("Width:           %d", width);
    LOGI ("Height:          %d", height);
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
        listener.setLensParameters (lensParams);
    }
    listener.initialize();

    // register a data listener
    ret = cameraDevice->registerDataListener (&listener);
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
    fill[0] = width;
    fill[1] = height;

    jintArray intArray = env->NewIntArray (2);

    env->SetIntArrayRegion (intArray, 0, 2, fill);

    return intArray;
}

void Java_com_esalman17_calibrator_MainActivity_RegisterCallback (JNIEnv *env, jobject thiz)
{
    // save JavaVM globally; needed later to call Java method in the listener
    env->GetJavaVM (&m_vm);

    m_obj = env->NewGlobalRef (thiz);

    // save refs for callback
    jclass g_class = env->GetObjectClass (m_obj);
    if (g_class == NULL)
    {
        std::cout << "Failed to find class" << std::endl;
    }

    // save method ID to call the method later in the listener
    m_amplitudeCallbackID = env->GetMethodID (g_class, "amplitudeCallback", "([I)V");
}

jboolean Java_com_esalman17_calibrator_MainActivity_StartCaptureNative (JNIEnv *env, jobject thiz)
{
    if (cameraDevice == nullptr)
    {
        LOGE ("There is no camera device to  start");
        return (jboolean)false;
    }
    auto ret = cameraDevice->startCapture();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to start capture, CODE %d", (int) ret);
        return (jboolean)false;
    }
    return (jboolean)true;
}

jboolean Java_com_esalman17_calibrator_MainActivity_StopCaptureNative (JNIEnv *env, jobject thiz)
{
    if (cameraDevice == nullptr)
    {
        LOGE ("There is no camera device to  stop");
        return (jboolean)false;
    }
    auto ret = cameraDevice->stopCapture();
    if (ret != CameraStatus::SUCCESS)
    {
        LOGE ("Failed to start capture, CODE %d", (int) ret);
        return (jboolean)false;
    }
    return (jboolean)true;
}

#ifdef __cplusplus
}
#endif
