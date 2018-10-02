package com.esalman17.calibrator;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.icu.text.Normalizer2;
import android.os.Bundle;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.graphics.Bitmap;
import android.graphics.Point;
import android.view.Display;

import java.util.HashMap;
import java.util.Iterator;

enum Mode{
    CAMERA,
    PROJECT,
    TEST,
}

public class MainActivity extends Activity {

    static {
        System.loadLibrary("usb_android");
        System.loadLibrary("royale");
        System.loadLibrary("nativelib");
    }

    private PendingIntent mUsbPi;
    private UsbManager manager;
    private UsbDeviceConnection usbConnection;

    private Bitmap bmpCam = null, bmpPr = null;
    Paint green = new Paint();
    Paint red = new Paint();
    Point marker;

    private ImageView mainImView;
    Button buttonAdd;

    boolean cam_opened, capturing=false;

    private static final String LOG_TAG = "MainActivity";
    private static final String ACTION_USB_PERMISSION = "ACTION_ROYALE_USB_PERMISSION";

    int[] resolution;
    Point displaySize, camRes;

    Mode currentMode = Mode.CAMERA;

    public native int[] OpenCameraNative(int fd, int vid, int pid);
    public native boolean StartCaptureNative();
    public native boolean StopCaptureNative();
    public native void RegisterCallback();
    public native void ChangeModeNative(int mode);

    //broadcast receiver for user usb permission dialog
    private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);

                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                    if (device != null) {
                        RegisterCallback();
                        performUsbPermissionCallback(device);
                        createBitmap();
                    }
                } else {
                    System.out.println("permission denied for device" + device);
                }
            }
        }
    };

    @SuppressLint("ClickableViewAccessibility")
    @Override
    public void onCreate(Bundle savedInstanceState) {
        Log.d(LOG_TAG, "onCreate()");
        super.onCreate(savedInstanceState);
        setRequestedOrientation (ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        getWindow().setBackgroundDrawableResource(R.color.black);
        setContentView(R.layout.activity_main);

        // hide the navigation bar
        final int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;

        getWindow().getDecorView().setSystemUiVisibility(flags);
        final View decorView = getWindow().getDecorView();
        decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility)
            {
                if((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0)
                {
                    decorView.setSystemUiVisibility(flags);
                }
            }
        });

        green.setColor(Color.GREEN);
        green.setStyle(Paint.Style.FILL);
        red.setColor(Color.RED);
        red.setStyle(Paint.Style.FILL_AND_STROKE);
        red.setStrokeWidth(3);

        mainImView =  findViewById(R.id.imageViewMain);
        mainImView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View view, MotionEvent event) {
                if(currentMode != Mode.PROJECT){
                    return false;
                }
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        marker = new Point((int) event.getX(), (int) event.getY());
                        Canvas canvas = new Canvas(bmpPr);
                        canvas.drawColor(0xFF000000);
                        int radius = 40;
                        canvas.drawCircle(marker.x, marker.y, radius, green);
                        canvas.drawLine(marker.x-radius, marker.y, marker.x+radius, marker.y, red);
                        canvas.drawLine(marker.x, marker.y-radius, marker.x, marker.y+radius, red);
                        mainImView.setImageBitmap(bmpPr);
                        break;
                }
                return true;
            }
        });

        findViewById(R.id.buttonCamera).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(!cam_opened) {
                    openCamera();
                }
                if(!capturing) startCapture();
                ChangeModeNative(1);
                currentMode = Mode.CAMERA;
                Log.i(LOG_TAG, "Mode changed: CAMERA");
                buttonAdd.setVisibility(View.GONE);
            }
        });

        findViewById(R.id.buttonProject).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(!cam_opened) {
                    openCamera();
                }
                if(!capturing) startCapture();
                ChangeModeNative(2);
                currentMode = Mode.PROJECT;
                Log.i(LOG_TAG, "Mode changed: PROJECT");

                if (bmpPr == null) {
                    bmpPr = Bitmap.createBitmap(displaySize.x, displaySize.y, Bitmap.Config.ARGB_8888);
                }
                buttonAdd.setVisibility(View.VISIBLE);
            }
        });

        buttonAdd = findViewById(R.id.buttonAdd);
        buttonAdd.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {

            }
        });

    }

    @Override
    protected void onPause() {
        Log.d(LOG_TAG, "onPause()");
        if (cam_opened) {
            if(StopCaptureNative()){
                capturing = false;
                Log.d(LOG_TAG, "Capture has stopped");
            }
        }
        super.onPause();
        unregisterReceiver(mUsbReceiver);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(LOG_TAG, "onResume()");
        registerReceiver(mUsbReceiver, new IntentFilter(ACTION_USB_PERMISSION));

        displaySize = new Point();
        getWindowManager().getDefaultDisplay().getRealSize(displaySize);
        Log.i(LOG_TAG, "Window display size: x=" + displaySize.x + ", y=" + displaySize.y);

        if(cam_opened) startCapture();
    }

    @Override
    protected void onDestroy() {
        Log.d(LOG_TAG, "onDestroy()");
        //unregisterReceiver(mUsbReceiver);

        if(usbConnection != null) {
            usbConnection.close();
        }

        super.onDestroy();
    }

    public void openCamera() {
        Log.d(LOG_TAG, "openCamera");

        //check permission and request if not granted yet
        manager = (UsbManager) getSystemService(Context.USB_SERVICE);

        if (manager != null) {
            Log.d(LOG_TAG, "Manager valid");
        }

        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();

        Log.d(LOG_TAG, "USB Devices : " + deviceList.size());

        Iterator<UsbDevice> iterator = deviceList.values().iterator();
        UsbDevice device;
        boolean found = false;
        while (iterator.hasNext()) {
            device = iterator.next();
            if (device.getVendorId() == 0x1C28 ||
                    device.getVendorId() == 0x058B ||
                    device.getVendorId() == 0x1f46) {
                Log.d(LOG_TAG, "royale device found");
                found = true;
                if (!manager.hasPermission(device)) {
                    Intent intent = new Intent(ACTION_USB_PERMISSION);
                    intent.setAction(ACTION_USB_PERMISSION);
                    mUsbPi = PendingIntent.getBroadcast(this, 0, intent, 0);
                    manager.requestPermission(device, mUsbPi);
                } else {
                    RegisterCallback();
                    performUsbPermissionCallback(device);
                    createBitmap();
                }
                break;
            }
        }
        if (!found) {
            Log.e(LOG_TAG, "No royale device found!!!");
        }
    }

    private void performUsbPermissionCallback(UsbDevice device) {
        usbConnection = manager.openDevice(device);
        Log.i(LOG_TAG, "permission granted for: " + device.getDeviceName() + ", fileDesc: " + usbConnection.getFileDescriptor());

        int fd = usbConnection.getFileDescriptor();

        resolution = OpenCameraNative(fd, device.getVendorId(), device.getProductId());
        Log.d(LOG_TAG, "Camera resolution: width="+resolution[0]+" height="+resolution[1]);
        camRes = new Point(resolution[0], resolution[1]);

        if (resolution[0] > 0) {
            cam_opened = true;
        }
    }

    public void startCapture() {
        if(cam_opened ){
            if(StartCaptureNative()){
                capturing = true;
                Log.d(LOG_TAG, "Camera is capturing");
            }
            else{
                capturing = false;
            }
        }
    }

    private void createBitmap() {
        if (bmpCam == null) {
            bmpCam = Bitmap.createBitmap(resolution[0], resolution[1], Bitmap.Config.ARGB_8888);
        }
    }

    public void amplitudeCallback(int[] amplitudes) {
        if (!cam_opened)
        {
            Log.d(LOG_TAG, "Device in Java not initialized");
            return;
        }
        if(currentMode == Mode.CAMERA){
            if(bmpCam == null){
                createBitmap();
            }
            bmpCam.setPixels(amplitudes, 0, resolution[0], 0, 0, resolution[0], resolution[1]);

            runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    mainImView.setImageBitmap(bmpCam);
                }
            });
        }
    }


}

