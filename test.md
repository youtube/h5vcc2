## How to Run the Test

To run the progressive playback and Picture-in-Picture (PiP) test on your device, follow these steps:

### Step 1: Push the test HTML to the device
Push the TV-friendly test page to the device's temporary directory and set the correct permissions:
```bash
adb push test.html /data/local/tmp/test.html
adb shell chmod 644 /data/local/tmp/test.html
```

### Step 2: Rebuild and Reinstall the APK
Since we modified the manifest (`AndroidManifest.xml.jinja2`), you must rebuild and reinstall the APK for the PiP enablement to take effect:
```bash
# Rebuild the QA build
autoninja -C out/android-arm_qa chrobalt_apk

# Reinstall on the device
adb install -r out/android-arm_qa/apks/Chrobalt.apk
```

### Step 3: Launch Chrobalt with the test URL
Force-stop any running instance and launch the main activity, passing the local file URL as a command-line argument:
```bash
# Force-stop
adb shell am force-stop dev.cobalt.coat

# Launch with test.html
adb shell am start -n dev.cobalt.coat/dev.cobalt.app.MainActivity --esa commandLineArgs "--url=file:///data/local/tmp/test.html"
```