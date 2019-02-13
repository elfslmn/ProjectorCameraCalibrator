//
// Created by esalman17 on 13.02.2019.
//

#include "opencv2/opencv.hpp"
#include "CamListener.h"

using namespace std;
using namespace cv;

class Calibrator : public CamListener{

    const int RETRO_THRESHOLD = 300;
    const double MAX_RETRO_AREA = 50.0;
    const int MIN_CONFIDENCE = 100;

    struct CamPoint{
        Point3f xyz;            // in cm
        Point2i uv;             // in pixel ( cam )
        Point2i uv_corrected;   // in pixel ( cam )
    };

public:
    // Constructors
    Calibrator();
    ~Calibrator();

    string dataFolder = "data";
    Vec4d calibration_result;

    enum Mode {UNKNOWN, DEPTH, GRAY, PROJECT, TEST};
    Mode currentMode = DEPTH;

    Device projector; //{1280, 720, 37.6*deg2rad, 21.76*deg2rad};

    //functions
    void saveFrame(int frame);
    bool saveCamPoint();
    Vec4d calibrate();
    Point2i convertCam2Pro(Point2i proj_point, float depth);

    void setProjector(int width, int height, double v_fov, double h_fov);
    void saveCalibrationToFile(string filepath);
    void setMode(int i);


private:
    Mat pattern, grayBin;
    vector<vector<Point> > retro_contours;
    vector<CamPoint> cam_points;

    float x_scale, y_scale;
    double x_offset, y_offset; // in pro. pixel
    vector<int> blobCenters;

    void onNewData (const DepthData *data);
    pair<double, double> fitExponential(const vector<double> &x, vector<double> &y);
    pair<double, double> fitLinear(const vector<double> &x, const vector<double> &y);
    void undistortCamPoints();

};

