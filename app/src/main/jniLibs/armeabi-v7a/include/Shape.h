#include <iostream>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

class Shape {
public:
   vector<Point> contour;
   bool isValidShape = true;
   // Constructors
   Shape(const vector<Point> & contour);

   // Getters
   double getArea();
   double getPerimeter();
   Point2f getCenter();
   Rect getBoundingRect();
   vector<Point> getApprox();
   string getType();

   vector<Point> approximatePolyDP(double epsilon);
   void draw(cv::Mat& image);

private:
   double area = -1;
   double perimeter = -1;
   Point2f center = Point2f(FLT_MAX, FLT_MAX);
   Rect boundingRect;
   vector<Point> approx;
   string type = "NULL";

};
