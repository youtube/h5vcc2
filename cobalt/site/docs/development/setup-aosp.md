Project: /youtube/cobalt/_project.yaml
Book: /youtube/cobalt/_book.yaml

# Set up your environment - AOSP

These instructions explain how to build and run Cobalt for the AOSP platform (`aosp-arm`, `aosp-arm64`, `aosp-x86`).

Before following these instructions, make sure you have set up your workstation and source checkout as described in [Set up your environment - Linux](setup-linux.md).

## Prerequisites

1. Follow all steps in [Set up your environment - Linux](setup-linux.md) to install basic system dependencies, `depot_tools`, clone the Cobalt repository, and run `build/install-build-deps.sh`.

2. Install host binutils packages for ARM cross-compilation symbol stripping:

   ```bash
   sudo apt install -y binutils-arm-linux-gnueabi    # For 32-bit aosp-arm
   # sudo apt install -y binutils-aarch64-linux-gnu # For 64-bit aosp-arm64
   ```

3. Ensure your root `.gclient` file includes `android` in `target_os`:

   ```python
   target_os = [ 'linux', 'android' ]
   ```

   Then synchronize dependencies from your top-level repository:

   ```bash
   cd ~/cobalt/src
   gclient sync
   ```

4. Set up an Android debug keystore required for signing development APKs:

   ```bash
   keytool -genkey -v -keystore ~/.android/debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000
   ```

## Running in Evergreen Mode

Because Evergreen support is required for certification, partners deploy official Google prebuilt `.crx` packages (available on [GitHub Releases](https://github.com/youtube/cobalt/releases)) into the Slot 0 directory structure, compile the `cobalt_loader` APK, and deploy to target hardware.

### Deploying Official Google Prebuilt CRX Packages (Primary Flow)

1. Ensure environment variables are set and initialize an AOSP build directory:

   ```bash
   export PATH="$HOME/depot_tools:$PATH"

   # For 32-bit ARM AOSP targets
   cobalt/build/gn.py -p aosp-arm -c qa --no-rbe

   # For 64-bit ARM AOSP targets
   # cobalt/build/gn.py -p aosp-arm64 -c qa --no-rbe
   ```

2. Build the application loader APK:

   ```bash
   autoninja -C out/aosp-arm_qa cobalt_loader
   ```

   This generates the application loader APK at `out/aosp-arm_qa/apks/cobalt.apk`.

3. Download the official prebuilt CRX file from [GitHub Releases](https://github.com/youtube/cobalt/releases):

   ```bash
   export LOCAL_CRX_DIR=/tmp/cobalt_dl
   rm -rf $LOCAL_CRX_DIR && mkdir -p $LOCAL_CRX_DIR

   # Download the prebuilt CRX corresponding to your target architecture (e.g. arm-softfp or arm64)
   COBALT_CRX_URL="https://github.com/youtube/cobalt/releases/download/<version>/cobalt_evergreen_<version>_arm-softfp_<config>.crx"
   wget $COBALT_CRX_URL -O $LOCAL_CRX_DIR/cobalt_prebuilt.crx
   ```

4. Unpack and inject the official prebuilt CRX into the compiled APK:

   ```bash
   unzip $LOCAL_CRX_DIR/cobalt_prebuilt.crx -d $LOCAL_CRX_DIR/cobalt_prebuilt

   # Inject prebuilt Slot 0 Core library and manifest into the APK assets
   cd $LOCAL_CRX_DIR/cobalt_prebuilt
   mkdir -p assets/app/cobalt/lib assets/app/cobalt/content
   cp -f manifest.json assets/app/cobalt/
   cp -rf lib/* assets/app/cobalt/lib/
   cp -rf content/* assets/app/cobalt/content/

   zip -u $OLDPWD/out/aosp-arm_qa/apks/cobalt.apk assets/app/cobalt/manifest.json assets/app/cobalt/lib/* assets/app/cobalt/content/*
   ```

5. Deploy and launch on an AOSP device or emulator:

   Ensure your device is connected via ADB (`adb devices` or `adb connect <device_ip>:5555`).

   Install the compiled APK:

   ```bash
   adb install -r out/aosp-arm_qa/apks/cobalt.apk
   ```

   Launch the application using `adb` (Package: `dev.cobalt.coat`, Activity: `dev.cobalt.app.MainActivity`):

   ```bash
   adb shell am start dev.cobalt.coat/dev.cobalt.app.MainActivity
   ```

   Pass runtime flags or custom URL using `--esa commandLineArgs`:

   ```bash
   adb shell am start --esa commandLineArgs 'url=https://www.youtube.com/tv' dev.cobalt.coat/dev.cobalt.app.MainActivity
   ```

   To force-stop any running instance before relaunching:

   ```bash
   adb shell am force-stop dev.cobalt.coat
   ```

---

### Compiling Custom Cobalt Core from Source (For Core Engine Debugging Only)

> [!CAUTION]
> SoC and OEM partners are required to use official Google Prebuilt CRX packages for testing and certification. Compiling Cobalt Core (`libcobalt.so`) from source is intended only for core developers debugging internal engine changes.

1. Configure the build directory for the target AOSP platform (`aosp-arm`, `aosp-arm64`, `aosp-x86`) using `cobalt/build/gn.py`.

   Use the `-c` flag to specify a `build_type` (`debug`, `devel`, `qa`, or `gold`).

   ```bash
   cobalt/build/gn.py -p aosp-arm -c devel --no-rbe
   ```

2. Compile the `cobalt_loader` target using `autoninja`:

   ```bash
   autoninja -C out/aosp-arm_devel cobalt_loader
   ```

   This generates the application loader APK at `out/aosp-arm_devel/apks/cobalt.apk`.

3. Deploy and launch on an AOSP device or emulator:

   Ensure your device is connected via ADB (`adb devices` or `adb connect <device_ip>:5555`).

   Install the compiled APK:

   ```bash
   adb install -r out/aosp-arm_devel/apks/cobalt.apk
   ```

   Launch the application using `adb` (Package: `dev.cobalt.coat`, Activity: `dev.cobalt.app.MainActivity`):

   ```bash
   adb shell am start dev.cobalt.coat/dev.cobalt.app.MainActivity
   ```

   Pass runtime flags using `--esa commandLineArgs`:

   ```bash
   adb shell am start --esa commandLineArgs 'url=https://www.youtube.com/tv' dev.cobalt.coat/dev.cobalt.app.MainActivity
   ```

   To force-stop any running instance before relaunching:

   ```bash
   adb shell am force-stop dev.cobalt.coat
   ```

## Running Tests

The No Platform Left Behind (NPLB) test suite verifies Starboard implementation on AOSP targets.

1. Compile the NPLB test suite:

   ```bash
   cobalt/build/gn.py -p aosp-arm -c devel --no-rbe
   autoninja -C out/aosp-arm_devel nplb_loader
   ```

   This generates the test APK at `out/aosp-arm_devel/apks/nplb.apk`.

2. Install the NPLB test APK on the target device:

   ```bash
   adb install -r out/aosp-arm_devel/apks/nplb.apk
   ```

3. Launch NPLB on device passing the target compressed library argument via `--esa commandLineArgs`:

   ```bash
   adb shell "am start --esa commandLineArgs '--evergreen_library=app/cobalt/lib/libnplb.lz4,--evergreen_content=app/cobalt/content' dev.cobalt.coat/dev.cobalt.app.MainActivity"
   ```

4. Pass standard Google Test filtering arguments:

   ```bash
   # Run NPLB with a specific test filter (e.g. Memory tests)
   adb shell "am start --esa commandLineArgs '--evergreen_library=app/cobalt/lib/libnplb.lz4,--evergreen_content=app/cobalt/content,--gtest_filter=*Memory*' dev.cobalt.coat/dev.cobalt.app.MainActivity"
   ```

## Debugging

To monitor log output, watch logcat with a filter for Starboard and Cobalt messages:

```bash
adb logcat -s "starboard:*" "Cobalt:*"
```

## Clean up or reset the environment

To clean build artifacts:

```bash
gn clean out/aosp-arm_devel
```
