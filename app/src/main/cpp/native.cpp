#include <royale/CameraManager.hpp>
#include <royale/ICameraDevice.hpp>
#include <iostream>
#include <jni.h>
#include <android/log.h>
#include <thread>
#include <chrono>
#include <mutex>
#include "opencv2/opencv.hpp"

#ifdef __cplusplus
extern "C"
{
#endif

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "Native", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "Native", __VA_ARGS__))
#define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, "Native", __VA_ARGS__))

using namespace royale;
using namespace std;
using namespace cv;

JavaVM *m_vm;
jmethodID m_amplitudeCallbackID;
jobject m_obj;

uint16_t width, height;
mutex flagMutex;

// this represents the main camera device object
static std::unique_ptr<ICameraDevice> cameraDevice;
Point3f retro;

enum Mode {UNKNOWN, CAMERA, PROJECT, TEST};
Mode currentMode;

struct Match{
    Point3f cam;
    Point2i proj;
};
vector<Match> matches;
Mat x;

bool calibrate(){
    int n = matches.size();
    if(n<6){
        LOGE("You need more than 6 points to calibrate");
        return false;
    }
    Mat A = Mat::zeros(2*n, 11, CV_32FC1);
    Mat y = Mat(2*n, 1, CV_32FC1);
    for(int i = 0; i<n; i++){
        Match m = matches[i];
        A.at<float>(i,0) = m.cam.x;
        A.at<float>(i,1) = m.cam.y;
        A.at<float>(i,2) = m.cam.z;
        A.at<float>(i,3) = 1;
        A.at<float>(i,8) = m.cam.x * m.proj.x*-1;
        A.at<float>(i,9) = m.cam.y* m.proj.x*-1;
        A.at<float>(i,10) = m.cam.z* m.proj.x*-1;

        A.at<float>(i+1,4) = m.cam.x;
        A.at<float>(i+1,5) = m.cam.y;
        A.at<float>(i+1,6) = m.cam.z;
        A.at<float>(i+1,7) = 1;
        A.at<float>(i+1,8) = m.cam.x * m.proj.y*-1;
        A.at<float>(i+1,9) = m.cam.y* m.proj.y*-1;
        A.at<float>(i+1,10) = m.cam.z* m.proj.y*-1;

        y.at<int>(i,0) = m.proj.x;
        y.at<int>(i+1,0) =m.proj.y;
    }
    solve(A, y, x, DECOMP_QR );

    ostringstream stream;
    stream << "x: ";
    for(int i=0; i<11; i++){
        stream << x.at<float>(i,0)<<",";
    }
    LOGI("%s", stream.str().c_str());
    return true;
}

class MyListener : public IDepthDataListener
{
    Mat cameraMatrix, distortionCoefficients;
    Mat zImage, grayImage, binaryImage;

    Mat drawing, norm;

    const float MAX_DISTANCE = 1.0f;

    void transferImage(const Mat& image)
    {
        jint fill[image.rows * image.cols];
        if(image.type() == 16) // CV_8UC3
        {
            //  int color = (A & 0xff) << 24 | (R & 0xff) << 16 | (G & 0xff) << 8 | (B & 0xff);
            int k = 0;
            for (int i = 0; i < image.rows; i++)
            {
                const Vec3b *ptr = image.ptr<Vec3b>(i);
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

    void transferGrayData(const DepthData* data)
     {
         int gray[ width * height];
         for (int i = 0; i < width * height; i++) {
             // use min value and span to have values between 0 and 255 (for visualisation)
             gray[i] = (int) (data->points.at(width * height - i -1).grayValue / 2.0f);
             if (gray[i] > 255) gray[i] = 255; // prevent exceed 255

             // set same value for red, green and blue; alpha to 255; to create gray image
             gray[i] = gray[i] | gray[i] << 8 | gray[i] << 16 | 255 << 24;
         }
         // attach to the JavaVM thread and get a JNI interface pointer
         JNIEnv *env;
         m_vm->AttachCurrentThread((JNIEnv **) &env, NULL);
         jintArray intGrayArray = env->NewIntArray(width * height);
         env->SetIntArrayRegion(intGrayArray, 0, width * height, gray);
         // trigger callback function in activity
         env->CallVoidMethod(m_obj, m_amplitudeCallbackID, intGrayArray);
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
            uint16_t *gRowPtr = image.ptr<uint16_t> (y);
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
        float sumX = 0.0f, sumY = 0.0f, sumZ = 0.0f;
        int count = 0;
        for(int k = 0; k < width*height; k++){
            auto curPoint = data->points.at(k);
            // retro screen has high gray values, take only the depth of these high gray pixels.
            if (curPoint.grayValue > 600 && curPoint.z > 0) { // z>0 to eliminate saturated gray values
                sumX += curPoint.x;
                sumY += curPoint.y;
                sumZ += curPoint.z;
                count++;
            }
        }

        if(count == 0){
            retro.x = .0;
            retro.y = .0;
            retro.z = .0;
        }
        else{
            retro.x = sumX / count;
            retro.y = sumY / count;
            retro.z = sumZ / count;
            //LOGD("Retro x=%.3fm y=%.3fm z=%.3fm", retro.x, retro.y, retro.z);
        }

        if(currentMode == CAMERA){
            transferGrayData(data);
        }
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
        //grayImage.create (Size (width,height), CV_16UC1);
        //drawing = Mat::zeros(height, width, CV_8UC3);
        retro = Point3f(.0,.0,.0);
    }

    void setMode(int i){
        lock_guard<mutex> lock (flagMutex);
        switch(i)
        {
            case 1:
                currentMode = CAMERA;
                LOGD("Mode: CAMERA");
                break;
            case 2:
                currentMode = PROJECT;
                LOGD("Mode: PROJECT");
                break;
            case 3:
                currentMode = TEST;
                LOGD("Mode: TEST");
                break;
            default:
                currentMode = UNKNOWN;
                LOGD("Mode: UNKNOWN (%d)", i);
                break;
        }
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
        LOGE ("Failed to stop capture, CODE %d", (int) ret);
        return (jboolean)false;
    }
    return (jboolean)true;
}

void Java_com_esalman17_calibrator_MainActivity_ChangeModeNative (JNIEnv *env, jobject thiz, jint mode)
{
    listener.setMode(mode);
}
jint Java_com_esalman17_calibrator_MainActivity_AddPointNative (JNIEnv *env, jobject thiz, jint x, jint y)
{
    lock_guard<mutex> lock (flagMutex);
    if(retro.z == 0){
        LOGE ("No retro found to add matched points");
        return (jint)-1;
    }
    Match m = {retro, Point2i(x,y)};
    matches.push_back(m);
    LOGI("Point added\t Camera: x=%.3fm y=%.3fm z=%.3fm\t Projector: x=%d y=%d", retro.x, retro.y, retro.z, x,y);
    return (jint)matches.size();
}

jboolean Java_com_esalman17_calibrator_MainActivity_CalibrateNative (JNIEnv *env, jobject thiz)
{
    return (jboolean)calibrate();
}

#ifdef __cplusplus
}
#endif
