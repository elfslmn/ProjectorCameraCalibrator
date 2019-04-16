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
import android.graphics.drawable.Drawable;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.icu.text.Normalizer2;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.graphics.Bitmap;
import android.graphics.Point;
import android.view.Display;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.Iterator;

enum Mode{
    DEPTH,
    GRAY,
    CALIBRATION,
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
    private Drawable pattern;
    private static Paint white = new Paint();

    private ImageView mainImView;
    Button buttonAdd, buttonCalc;
    TextView tvDebug;

    SimpleDateFormat parser = new SimpleDateFormat("yyyy_MM_dd_HH_mm");

    boolean cam_opened, capturing=false;

    private static final String LOG_TAG = "MainActivity";
    private static final String ACTION_USB_PERMISSION = "ACTION_ROYALE_USB_PERMISSION";

    int[] resolution;
    Point displaySize, camRes;
    boolean camFlip = true;

    Mode currentMode = Mode.GRAY;

    private static final int PICKFILE_REQUEST_CODE = 1;

    public native int[] OpenCameraNative(int fd, int vid, int pid);
    public native boolean StartCaptureNative();
    public native boolean StopCaptureNative();
    public native void RegisterCallback();
    public native void ChangeModeNative(int mode);
    public native boolean AddPointNative();
    public native double[] CalibrateNative();
    public native void ToggleFlipNative();
    public native void LoadCalibrationNative(double[] calibration);

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
                        if (bmpCam == null) {
                            bmpCam = Bitmap.createBitmap(resolution[0], resolution[1], Bitmap.Config.ARGB_8888);
                        }
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

        white.setColor(Color.WHITE);
        white.setStyle(Paint.Style.STROKE);
        white.setStrokeWidth(5);

        pattern = getResources().getDrawable(R.drawable.pattern);
        mainImView = findViewById(R.id.imageViewMain);
        tvDebug = findViewById(R.id.textViewDebug);

        //--- Click Listeners ---
        findViewById(R.id.buttonDepth).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(!cam_opened){
                    openCamera();
                }
                if(!capturing){
                    startCapture();
                }
                ChangeModeNative(1);
                currentMode = Mode.DEPTH;
                buttonCalc.setVisibility(View.GONE);
                buttonAdd.setVisibility(View.GONE);
                tvDebug.setText("Mode: DEPTH");
            }
        });

        findViewById(R.id.buttonGray).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(!cam_opened){
                    openCamera();
                }
                if(!capturing){
                    startCapture();
                }
                ChangeModeNative(2);
                currentMode = Mode.GRAY;
                buttonCalc.setVisibility(View.GONE);
                buttonAdd.setVisibility(View.GONE);
                tvDebug.setText("Mode: GRAY");
            }
        });

        findViewById(R.id.buttonCalib).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(!cam_opened) {
                    openCamera();
                }
                if(!capturing){
                    startCapture();
                }
                ChangeModeNative(3);
                currentMode = Mode.CALIBRATION;

                mainImView.setImageDrawable(pattern);

                buttonAdd.setVisibility(View.VISIBLE);
                buttonCalc.setVisibility(View.VISIBLE);
                tvDebug.setText("Mode: CALIBRATION");
            }
        });

        findViewById(R.id.buttonTest).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if(!cam_opened) {
                    openCamera();
                }
                if(!capturing){
                    startCapture();
                }
                ChangeModeNative(4);
                currentMode = Mode.TEST;
                Log.i(LOG_TAG, "Mode changed: TEST");
                buttonCalc.setVisibility(View.GONE);
                buttonAdd.setVisibility(View.GONE);
            }
        });

        findViewById(R.id.buttonFlip).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                ToggleFlipNative();
                camFlip = !camFlip;
            }
        });

        findViewById(R.id.buttonLoad).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
                Uri uri = Uri.parse(Environment.getExternalStorageDirectory().getAbsolutePath()
                        + "/Calibrator/");
                intent.setDataAndType(uri, "text/txt");
                startActivityForResult(Intent.createChooser(intent,"Open folder"), PICKFILE_REQUEST_CODE);
            }
        });

        buttonAdd = findViewById(R.id.buttonAdd);
        buttonAdd.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                boolean res = AddPointNative();
                if(res){
                    Toast.makeText(getApplicationContext(), "Point is added", Toast.LENGTH_SHORT).show();
                }
                else{
                    Toast.makeText(getApplicationContext(), "Point cannot be added", Toast.LENGTH_SHORT).show();
                }
            }
        });

        buttonCalc = findViewById(R.id.buttonCalc);
        buttonCalc.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                double[] calibration = CalibrateNative();
                saveCalibrationResult(calibration);
            }
        });

    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(LOG_TAG, "onResume()");
        registerReceiver(mUsbReceiver, new IntentFilter(ACTION_USB_PERMISSION));

        displaySize = new Point();
        getWindowManager().getDefaultDisplay().getRealSize(displaySize);
        // Make sure that is 1280,720
        Log.i(LOG_TAG, "Window display size: x=" + displaySize.x + ", y=" + displaySize.y);

        if(cam_opened && !capturing){
            startCapture();
        }
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
    protected void onDestroy() {
        Log.d(LOG_TAG, "onDestroy()");

        if(usbConnection != null) {
            usbConnection.close();
        }

        super.onDestroy();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        switch (requestCode) {
            case PICKFILE_REQUEST_CODE:
                if (resultCode == RESULT_OK) {
                    String path = data.getData().getPath();
                    Log.d(LOG_TAG, "Calibration file loaded: " + path);
                    Toast.makeText(this, "Calibration file loaded:" + path, Toast.LENGTH_SHORT).show();
                    File file = new File(path);
                    try {
                        BufferedReader br = new BufferedReader(new FileReader(file));
                        String line;
                        double[] calibration = new double[4];
                        int i = 0;
                        while ((line = br.readLine()) != null) {
                            calibration[i++] = Double.parseDouble(line);
                            if(i == 4) break;
                        }
                        Log.d(LOG_TAG, "Calibration array = "+ Arrays.toString(calibration));
                        LoadCalibrationNative(calibration);
                    } catch (FileNotFoundException e) {
                        e.printStackTrace();
                    } catch (IOException e) {
                        e.printStackTrace();
                    }
                }
                break;
        }
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
                    if (bmpCam == null) {
                        bmpCam = Bitmap.createBitmap(resolution[0], resolution[1], Bitmap.Config.ARGB_8888);
                    }
                }
                break;
            }
        }
        if (!found) {
            Log.e(LOG_TAG, "No royale device found!!!");
            Toast.makeText(getApplicationContext() ,"No camera found", Toast.LENGTH_SHORT).show();
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

    public void amplitudeCallback(int[] amplitudes) {
        if(currentMode == Mode.CALIBRATION || currentMode == Mode.TEST){
            // This callback should not be called in this modes
            return;
        }
        if (!cam_opened) {
            Log.d(LOG_TAG, "Device in Java not initialized");
            return;
        }

        if(bmpCam == null){
            bmpCam = Bitmap.createBitmap(resolution[0], resolution[1], Bitmap.Config.ARGB_8888);
        }
        bmpCam.setPixels(amplitudes, 0, resolution[0], 0, 0, resolution[0], resolution[1]);

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mainImView.setImageBitmap(bmpCam);
            }
        });
    }

    public void blobsCallback(final int[] descriptors) {
        if(currentMode != Mode.TEST){
            // This callback only be called in test mode
            return;
        }
        if (!cam_opened)
        {
            Log.d(LOG_TAG, "Device in Java not initialized");
            return;
        }
        if (bmpPr == null) {
            bmpPr = Bitmap.createBitmap(displaySize.x, displaySize.y, Bitmap.Config.ARGB_8888);
        }

        Canvas canvas = new Canvas(bmpPr);
        canvas.drawColor(0xFF000000); // to clear
        canvas.drawRect(0,0,1280,720, white);

        int x,y;
        for (int i = 0; i <= descriptors.length - 2; i += 2)
        {
            x = descriptors[i];
            y = descriptors[i+1];
            canvas.drawCircle(x, y, 40, white);
            canvas.drawCircle(x, y, 5, white);
        }

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mainImView.setImageBitmap(bmpPr);
            }
        });
    }

    private void saveCalibrationResult(double[] calibration){
        File sdcard = Environment.getExternalStorageDirectory();
        File dir = new File(sdcard.getAbsolutePath() + "/Calibrator/");
        dir.mkdir();

        File file = new File(dir, parser.format(new Date())+ (camFlip ? "_flipped":"")  +".txt");
        try {
            FileOutputStream out = new FileOutputStream(file);
            for(double d: calibration){
                out.write((d+"\n").getBytes());
            }
            out.flush();
            out.close();
            Log.d(LOG_TAG, "Results are saved into "+ file.getName());
        }
        catch (Exception e) {
            Toast.makeText(MainActivity.this, "Calibration cannot be saved", Toast.LENGTH_LONG).show();
            e.printStackTrace();
            return;
        }
        Toast.makeText(MainActivity.this, "Calibration is saved", Toast.LENGTH_LONG).show();
    }


}

