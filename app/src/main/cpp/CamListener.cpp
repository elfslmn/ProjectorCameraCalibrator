//
// Created by esalman17 on 5.10.2018.
//

#include "CamListener.h"
#include "Util.h"

CamListener::CamListener(){}

void CamListener::setCamera(int width, int height, double v_fov, double h_fov)
{
    grayImage.create (Size (width,height), CV_16UC1);
    xyzMap.create(Size (width,height), CV_32FC3);
    confMap.create(Size (width,height), CV_8UC1);

    camera.width = width;
    camera.height = height;
    camera.vertical_fov = v_fov * deg2rad;
    camera.horizontal_fov = h_fov * deg2rad;
    LOGD("Camera setted: w,h  %d , %d \t fov = %.2f , %.2f", width, height,v_fov,h_fov);
}

void CamListener::setFlip(bool f)
{
    lock_guard<mutex> lock (flagMutex);
    flip = f;
}

void CamListener::toggleFlip()
{
    if(flip) setFlip(false);
    else setFlip(true);
}


void CamListener::setLensParameters (LensParameters lensParameters)
{
    // Construct the camera matrix
    // (fx   0    cx)
    // (0    fy   cy)
    // (0    0    1 )
    lensParameters.principalPoint.first = camera.width - lensParameters.principalPoint.first; // due to camera flip
    lensParameters.principalPoint.second = camera.height - lensParameters.principalPoint.second;
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


void CamListener::onNewData (const DepthData *data)
{
    lock_guard<mutex> lock (flagMutex);
    updateMaps(data);
    // process images in here ...

    // for example
    vector<Mat> channels(3);
    split(xyzMap, channels);
    applyColorMap(channels[2], outputImage, COLORMAP_JET);
    callbackManager.sendImageToJavaSide(outputImage, flip);
}

// not use this flip, it messes the lens params, flip the image while sending to java side
void CamListener::updateMaps(const DepthData* data)
{
    int k;
    if(flip) k = camera.height * camera.width -1 ;
    else k = 0;

    for (int y = 0; y < camera.height ; y++)
    {
        Vec3f *xyzptr = xyzMap.ptr<Vec3f>(y);
        uint8_t *confptr = confMap.ptr<uint8_t>(y);
        uint16_t *grayptr= grayImage.ptr<uint16_t> (y);

        for (int x = 0; x < camera.width ; x++)
        {
            auto curPoint = data->points.at (k);
            xyzptr[x][0] = curPoint.x;
            xyzptr[x][1] = curPoint.y;
            xyzptr[x][2] = curPoint.z;
            confptr[x] = curPoint.depthConfidence;
            grayptr[x] = curPoint.grayValue;

            k = flip ? k-1 : k+1;
        }

    }
}
