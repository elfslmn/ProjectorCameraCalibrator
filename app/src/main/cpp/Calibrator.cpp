//
// Created by esalman17 on 13.02.2019.
//

#include "Calibrator.h"
#include "Util.h"

Calibrator::Calibrator()
{
    //LOGD("Calibrator is created.");
    pattern = Mat::zeros(101,101,CV_8UC1);
    rectangle(pattern, Point(0,0), Point(101,101), Scalar(255));
    line(pattern, Point(0,50),Point(101,50),Scalar(255));
    line(pattern, Point(50,0),Point(50,101),Scalar(255));
}

Calibrator::~Calibrator()
{
    //saveCalibrationToFile(dataFolder + "/cam_points.csv");
    //LOGD("Calibrator is destroyed.");
}

void Calibrator::setMode(int i){
    lock_guard<mutex> lock (flagMutex);
    switch(i)
    {
        case 1:
            currentMode = DEPTH;
            LOGD("Mode: DEPTH");
            break;
        case 2:
            currentMode = GRAY;
            LOGD("Mode: GRAY");
            break;
        case 3:
            currentMode = CALIBRATION;
            LOGD("Mode: CALIBRATION");
            break;
        case 4:
            currentMode = TEST;
            LOGD("Mode: TEST");
            break;
        default:
            currentMode = UNKNOWN;
            LOGD("Mode: UNKNOWN (%d)", i);
            break;
    }
}

void Calibrator::setProjector(int width, int height, double v_fov, double h_fov)
{
    projector.width = width;
    projector.height = height;
    projector.vertical_fov = v_fov * deg2rad;
    projector.horizontal_fov = h_fov * deg2rad;
    LOGD("Projector setted: w,h  %d , %d \t fov = %.2f , %.2f", width, height,v_fov,h_fov);
}

void Calibrator::onNewData (const DepthData *data)
{
    lock_guard<mutex> lock (flagMutex);
    updateMaps(data);

    // Only show depth map and return, no need for retro finding
    if(currentMode == DEPTH){
        vector<Mat> channels(3);
        split(xyzMap, channels);
        channels[2].at<float>(0,0) = MAX_RANGE;
        normalize(channels[2], channels[2], 0, 255, NORM_MINMAX, CV_8UC1);
        applyColorMap(channels[2], outputImage, COLORMAP_JET);
        callbackManager.sendImageToJavaSide(outputImage, flip);
        return;
    }

    // Find retro blobs
    threshold(grayImage, grayBin, RETRO_THRESHOLD, 255, CV_THRESH_BINARY);
    grayBin.convertTo(grayBin, CV_8UC1);
    findContours(grayBin, retro_contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, Point(0, 0));

    if(currentMode == GRAY){
        normalize(grayImage, outputImage, 0, 255, NORM_MINMAX, CV_8UC1);
        cvtColor(outputImage, outputImage, COLOR_GRAY2BGR);
        for( int i = 0; i< (int)retro_contours.size(); i++)
        {
            drawContours( outputImage, retro_contours, i, Scalar(255,0,255),-1,8);
        }
        callbackManager.sendImageToJavaSide(outputImage, flip);
    }
    else if (currentMode == CALIBRATION){
        // Do nothing
    }
    else if(currentMode == TEST){
        vector<Point2f> distorted, undistorted;
        blobCenters.clear();
        for( int i = 0; i< (int)retro_contours.size(); i++)
        {
            Rect brect = boundingRect(retro_contours[i]);
            Point2i raw = Point2i(brect.x + brect.width/2,  brect.y + brect.height/2 );
            distorted.push_back(raw);
        }

        if(distorted.size()){
            undistortPoints(distorted, undistorted, cameraMatrix, distortionCoefficients,cameraMatrix);
        }

        for(Point2f undist : undistorted )
        {
            float depth = xyzMap.at<Point3f>(undist.y, undist.x).z*100;
            Point2i corrected = convertCam2Pro(undist, depth);
            if(corrected.x == -1) continue;
            blobCenters.push_back(corrected.x);      // u (px)
            blobCenters.push_back(corrected.y);     // v (px)
        }

        callbackManager.onShapeDetected(blobCenters);
    }

}

bool Calibrator::saveCamPoint()
{
    lock_guard<mutex> lock (flagMutex);
    if(retro_contours.size() == 1)
    {
        double area = contourArea(retro_contours[0]);
        if( area > MAX_RETRO_AREA){
            LOGD("Retro area(%f) is above maximum(%f). Pair cannot be added.",area, MAX_RETRO_AREA);
            return false;
        }

        CamPoint cp;
        Rect brect = boundingRect(retro_contours[0]);
        cp.uv = Point2i(brect.x + brect.width/2,  brect.y + brect.height/2 );
        if(confMap.at<uint8_t>(cp.uv.y, cp.uv.x) < MIN_CONFIDENCE){
            LOGD("Confidence value of retro center is below minimum");
            return false;
        }

        cp.xyz = xyzMap.at<Point3f>(cp.uv.y, cp.uv.x)*100;
        cam_points.push_back(cp);
        LOGD("Cam point added : (u,v)=(%d,%d)\t(x,y,z)=(%.2f\t%.2f\t%.2f)",
             cp.uv.x, cp.uv.y, cp.xyz.x, cp.xyz.y, cp.xyz.z);
        LOGD("There are %d cam points saved", (int)cam_points.size());

        return true;
    }
    else
    {
        LOGD("None or multiple retro found. Pair cannot be added. size=%d", (int)retro_contours.size());
        return false;
    }
}

void Calibrator::undistortCamPoints()
{
    if(cam_points.rbegin()->uv_corrected.x != 0){
        return; // All points are corrected already
    }
    vector<Point2f> distorted;
    vector<Point2f> undistorted;
    for(auto cp : cam_points){
        distorted.push_back(cp.uv);
    }
    undistortPoints(distorted, undistorted, cameraMatrix, distortionCoefficients,cameraMatrix);
    for(int i = 0; i < (int)cam_points.size(); i++){
        cam_points[i].uv_corrected = undistorted[i];
    }
    LOGD("%d cam points are undistorted", (int)undistorted.size());
}

// x_shift = cx*e^(ax*z)
// y_shift = cy*e^(ay*z)
// z -> in x axis
// x or y -> in y axis
// return { cx, ax, cy, ay }
Vec4d Calibrator::calibrate()
{
    undistortCamPoints();
    //scale = sin(camFov/2) / sin(projFov/2)
    x_scale = sin(camera.vertical_fov/2) / sin(projector.vertical_fov/2);
    y_scale = sin(camera.horizontal_fov/2) / sin(projector.horizontal_fov/2);

    x_offset = (double)projector.width * (x_scale -1) / 2 ;
    y_offset = (double)projector.height * (y_scale -1) / 2;

    vector<double> x_shift;
    vector<double> y_shift;
    vector<double> depth;
    /*ofstream file;
    file.open(dataFolder + "/shift.csv");
    file << "z,x_shift(xp),y_shift(xp)\n";*/
    for(auto cp : cam_points)
    {
        double cam_x = cp.uv_corrected.x * projector.width * x_scale / camera.width - x_offset;
        double cam_y = cp.uv_corrected.y * projector.height * y_scale / camera.height - y_offset;

        x_shift.push_back(cam_x - projector.width / 2); // /2 since retro will be center of the projector
        y_shift.push_back(cam_y - projector.height / 2);
        depth.push_back((double)cp.xyz.z);
        //file << cp.xyz.z << "," << cam_x - projector.width / 2 << "," << cam_y - projector.height / 2 << endl;
    }
    //file.close();

    auto coeff_x = fitExponential(depth, x_shift);
    auto coeff_y = fitExponential(depth, y_shift);
    calibration_result = Vec4d(coeff_x.first, coeff_x.second, coeff_y.first, coeff_y.second);

    /*file.open(dataFolder + "/calibration.txt");
    file << "{ ax, bx, ay, by } = " << calibration_result << endl;
    file << "Scale x,y = " << x_scale << " , " << y_scale << endl;
    file << "Offset x,y = " << x_offset << " , " << y_offset << endl;
    file << " Camera: " << camera.width <<" , "<<camera.height<<" , "<<camera.vertical_fov<<" , "<<camera.horizontal_fov << endl;
    file << " Projector: " << projector.width <<" , "<<projector.height<<" , "<<projector.vertical_fov<<" , "<<projector.horizontal_fov << endl;
    file.close();*/

    return calibration_result;
}

// y = c*e^(a*x)
// x and y should not  be negative. (x will be depths, already positive, check only y)
pair<double, double> Calibrator::fitExponential(const vector<double> &x, vector<double> &y){
    int n = x.size();
    double lny[n];
    bool neg = false;
    for (int i = 0; i < n; i++){
        if(y[i] < 0 ){
            neg = true;
            y[i] *= -1;
        }
        lny[i]=log(y[i]);
    }
    double xsum = 0, x2sum = 0, ysum = 0, xysum = 0;
    for (int i = 0; i < n; i++){
        xsum += x[i];            //calculate sum(xi)
        ysum += lny[i];          //calculate sum(yi)
        x2sum += pow(x[i],2);    //calculate sum(x^2i)
        xysum += (x[i]*lny[i]);  //calculate sum(xi*yi)
    }

    double a = ( n*xysum - xsum*ysum ) / ( n*x2sum - xsum*xsum );
    double b = ( x2sum*ysum - xsum*xysum ) / ( x2sum*n - xsum*xsum );
    double c = pow(2.71828, b);
    if(neg){
        c *= -1;
    }

    double y_fit[n];
    double rss = 0; // residual sum of squares
    double mean = accumulate(y.begin(), y.end(), 0) / n;
    double var = 0;
    for (int i = 0; i < n; i++){
        y_fit[i] = c*pow(2.71828, a*x[i]);
        rss += pow(y_fit[i] - y[i], 2);
        var += pow(mean - y[i], 2);
    }
    double r2 = 1 - (rss / var);

    LOGD("Line fitted to %d points. R2 = %.5f", n, r2);
    LOGD("y = c*e^(a*x)  c = %.5f \t a = %.5f", c, a);

    return {c,a};
}

// {a,b}  y = ax + b
pair<double, double> Calibrator::fitLinear(const vector<double> &x, const vector<double> &y){
    int n = x.size();

    double xsum = 0, x2sum = 0, ysum = 0, xysum = 0;
    for (int i = 0; i < n; i++){
        xsum += x[i];            //calculate sum(xi)
        ysum += y[i];          //calculate sum(yi)
        x2sum += pow(x[i],2);    //calculate sum(x^2i)
        xysum += (x[i]*y[i]);  //calculate sum(xi*yi)
    }

    double a = ( n*xysum - xsum*ysum ) / ( n*x2sum - xsum*xsum );
    double b = ( x2sum*ysum - xsum*xysum ) / ( x2sum*n - xsum*xsum );

    double y_fit[n];
    double rss = 0; // residual sum of squares
    double mean = accumulate(y.begin(), y.end(), 0) / n;
    double var = 0;
    for (int i = 0; i < n; i++){
        y_fit[i] = a*x[i]+b;
        rss += pow(y_fit[i] - y[i], 2);
        var += pow(mean - y[i], 2);
    }
    double r2 = 1 - (rss / var);

    LOGD("Line fitted to %d points. R2 = %.5f", n, r2);
    LOGD("y = ax + b  a = %.5f \t b = %.5f", a, b);

    return {a,b};
}

Point2i Calibrator::convertCam2Pro(Point2i pp, float depth){
    if(x_scale == 0){ // No calibration calculated yet.
        LOGE("No calibration found");
        return Point2i(-1,-1);
    }

    if( pp.x<0 || pp.y<0 || depth <= 0){ // x and y in pixel
        return Point2i(-1,-1);
    }

    Vec4d & coef = calibration_result;
    double shiftx = coef[0] * exp(coef[1]*depth);
    double shifty = coef[2] * exp(coef[3]*depth);
    int cpx = (double)pp.x * projector.width* x_scale / camera.width - x_offset - shiftx;
    int cpy = (double)pp.y * projector.height* y_scale / camera.height - y_offset - shifty;

    if(flip){
        cpx = projector.width - cpx;
        cpy = projector.height - cpy;
    }

    if(cpx > projector.width || cpx < 0 || cpy > projector.height || cpy < 0){
        LOGD("Point is outside of the projector view");
        return Point2i(-1,-1);
    }
    return Point2i(cpx,cpy);
}

