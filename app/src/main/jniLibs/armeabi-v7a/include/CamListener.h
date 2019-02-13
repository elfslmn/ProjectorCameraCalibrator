//
// Created by esalman17 on 5.10.2018.
//

#include <royale/LensParameters.hpp>
#include <royale/IDepthDataListener.hpp>
#include "opencv2/opencv.hpp"
#include "CallbackManager.h"
#include <mutex>
#include <jni.h>

using namespace royale;
using namespace std;
using namespace cv;

static const double deg2rad = 0.0174533; // 1 degree = 0.0174533 radian

class CamListener : public royale::IDepthDataListener {

public:
    // Constructors
    CamListener();

    // Public methods
    void setLensParameters (LensParameters lensParameters);
    void setCamera(int width, int height, double v_fov, double h_fov);

    // Public variables
    CallbackManager callbackManager;

protected:
    struct Device{
        int width, height;  // in pixel
        double vertical_fov; // in radian- full fov
        double horizontal_fov;
    };


    virtual void onNewData (const DepthData *data);
    void updateMaps(const DepthData* data, bool flip=false);

    Mat xyzMap, confMap, grayImage;
    Mat outputImage; // to visualize with CV_8UC1

    Mat cameraMatrix, distortionCoefficients;

    Device camera;
    mutex flagMutex;
};

