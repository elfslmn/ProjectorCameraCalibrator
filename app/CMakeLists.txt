# set the required cmake version for this project
cmake_minimum_required( VERSION 3.6 )

# set the language level of C++-Files and C-Files
set( CMAKE_C_STANDARD 99 )
set( CMAKE_CXX_STANDARD 11 )

add_definitions(-DTARGET_PLATFORM_ANDROID)

# set the path to the royale header-Files
include_directories( "${CMAKE_CURRENT_SOURCE_DIR}/src/main/jniLibs/${ANDROID_ABI}/include" )
#OpenCV
include_directories(C:/opencv_androidSDK/OpenCV-android-sdk/sdk/native/jni/include)

# set the path to the royale libraries
link_directories( "${CMAKE_CURRENT_SOURCE_DIR}/src/main/jniLibs/${ANDROID_ABI}" )

add_library( nativelib SHARED src/main/cpp/native.cpp )

# set the target library to build and it's dependencies to be linked and compiled
target_link_libraries( nativelib

                       # android libraries
                       android
                       log

                       opencv_java3

                       # royale libraries
                       royale
                       spectre3
                       usb_android )