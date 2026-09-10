Project: /youtube/cobalt/_project.yaml
Book: /youtube/cobalt/_book.yaml

# Set up your environment: Cobalt 27.lts for RDK

These instructions explain how to set up the build environment and build Cobalt 27.lts for the RDK platform (`evergreen-arm-hardfp-rdk`). The steps are verified on a clean Ubuntu 22.04 environment and on the Amlogic (AH212) reference device.

Starboard for RDK is integrated into the Cobalt 27.lts branch. You can build the RDK `loader_app` (which includes Starboard) using the Cobalt build mechanism. These instructions explain how to build it and update the library on an RDK device.

The source for the RDK Starboard implementation is in the [RDK Central repository](https://github.com/rdkcentral/larboard). Cobalt merges and customizes it in [`starboard/contrib/rdk/`](https://github.com/youtube/cobalt/tree/27.lts/starboard/contrib/rdk).

## Prerequisites

These instructions assume you are running Ubuntu 22.04. Required libraries might vary depending on your Linux distribution.

### Install Initial Dependencies
First, install the basic tools required to download the toolchain and manage packages.

```
sudo apt update
sudo apt install -y wget curl git python3-dev xz-utils lsb-release file sudo
```

### Get an RDK device running the correct image
You need a YouTube reference device (Amlogic S905X4 (AH212)) to run the commands.

Retrieve the device information from the YouTube Partner portal: [AH212](https://developers.google.com/youtube/devices/living-room/compliance/ah212). Note that if you do not have access to the page above, please see [Accessing Living Room Partnership Resources](https://developers.google.com/youtube/devices/living-room/access/accessing-lr-partnership-resources).

More information about the RDK reference box can be found on the RDK Central wiki: [RDK-Google IPSTB profile stack on Amlogic reference board](https://wiki.rdkcentral.com/display/RDK/RDK-Google+IPSTB+profile+stack+on+Amlogic+reference+board).

## Build Cobalt Starboard for RDK

### Install RDK Toolchain

Install the specific RDK toolchain required to build Cobalt.

1.  **Define environment variables**:

    ```
    export RDK_HOME=$HOME/rdk/toolchain
    ```

2.  **Create the target directory**:

    ```
    mkdir -p ${RDK_HOME}
    ```

3.  **Download and install the toolchain**:

    ```
    wget https://storage.googleapis.com/cobalt-static-storage-public/20250521_rdk-glibc-x86_64-arm-toolchain-2.0.sh
    sh 20250521_rdk-glibc-x86_64-arm-toolchain-2.0.sh -d ${RDK_HOME} -y
    ```

### Clone depot_tools

Chromium and Cobalt use `depot_tools` for package management and code retrieval.

```
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git ~/depot_tools
export PATH="$HOME/depot_tools:$PATH"
```

### Download Cobalt Source Code

Clone the Cobalt source code repository and checkout the `27.lts` branch.

```
mkdir -p ~/chromium
cd ~/chromium
git clone --branch 27.lts --single-branch https://github.com/youtube/cobalt.git src
git -C src remote add _gclient https://github.com/youtube/cobalt.git
```

### Sync Subprojects

Use `gclient` to fetch all external dependencies required by Cobalt.

```
cd ~/chromium
gclient config --unmanaged --name=src https://github.com/youtube/cobalt.git
gclient sync --no-history -r src@$(git -C src rev-parse @)
```

### Install Build Dependencies

Run the installer script provided by Cobalt to install all necessary system libraries.

```
cd ~/chromium/src

# Run installer
./build/install-build-deps.sh

# Install sysroot for ARM
python3 build/linux/sysroot_scripts/install-sysroot.py --arch=arm
```

### Build Cobalt binary for the RDK platform

Generate the build files and compile Cobalt:

1.  **Ensure environment variables are set**:

    ```
    export PATH="$HOME/depot_tools:$PATH"
    export RDK_HOME=$HOME/rdk/toolchain
    ```

2.  **Generate build files**:

    ```
    cobalt/build/gn.py -p evergreen-arm-hardfp-rdk -c qa --no-rbe
    ```

3.  **Compile targets**:

    ```
    autoninja -C out/evergreen-arm-hardfp-rdk_qa/ cobalt_loader nplb_loader
    ```


### Generate Archive

Bundle the build artifacts into a tarball for deployment.

```
tar -czvf archive.tar.gz -C out/evergreen-arm-hardfp-rdk_qa/ \
  -T out/evergreen-arm-hardfp-rdk_qa/cobalt_loader.runtime_deps libloader_app.so \
  -T out/evergreen-arm-hardfp-rdk_qa/nplb_loader.runtime_deps gen/build_info.json
```

## Using Cobalt

To run Cobalt in executable mode: push the built archive to the device, extract it, configure the environment variables, set up the display layer, and launch the application from the console.

### Push the Archive to Device

You can use either `adb` or `scp` to push the `archive.tar.gz` to the device's `/data` directory.

**Option A: Using ADB**

```
adb push path/to/c27/archive.tar.gz /data/archive.tar.gz
```

**Option B: Using SCP**

```
scp path/to/c27/archive.tar.gz root@{RDK IP address}:/data/archive.tar.gz
```

### Extract the Archive on Device

Connect to the device and extract the archive to `/data/out_cobalt/`:

```
# Clean up previous files and create directory
rm -rf /data/out_cobalt && mkdir -p /data/out_cobalt

# Extract the archive
cd /data && tar -xzvf archive.tar.gz -C out_cobalt
```


### Set Up Environment Variables on Device
Configure the environment variables on the device before launching Cobalt:

```
export WESTEROS_GL_USE_BEST_MODE=1
export WESTEROS_GL_MAX_MODE=3840x2160
export WESTEROS_GL_GRAPHICS_MAX_SIZE=1920x1080
export WESTEROS_GL_USE_AMLOGIC_AVSYNC=1
export WESTEROS_GL_REFRESH_PRIORITY=F,80
export WESTEROS_SINK_AMLOGIC_USE_DMABUF=1
export WESTEROS_SINK_USE_FREERUN=1
export WESTEROS_SINK_USE_ESSRMGR=1
export WESTEROS_GL_USE_REFRESH_LOCK=1
export WESTEROS_GL_USE_UEVENT_HOTPLUG=1
export RDKSHELL_KEYMAP_FILE=/etc/rdkshell_keymapping.json
export ESSOS_NO_EVENT_LOOP_THROTTLE=1
export AVPK_SKIP_HDMI_VALIDATION=1
echo 0 > /sys/module/drm/parameters/vblankoffdelay
echo 1 > /sys/module/aml_drm/parameters/video_axis_zoom
export LD_PRELOAD=/usr/lib/libwesteros_gl.so.0.0.0
export XDG_RUNTIME_DIR="/run"
export WAYLAND_DISPLAY=test-0
```

### Create Display and Set Focus
Create the display layer and set focus on the device. This registers the `test-0` display layer and routes system inputs to it:

```
curl 'http://127.0.0.1:9998/jsonrpc' -d '{"jsonrpc": "2.0","id": 4,"method": "org.rdk.RDKShell.1.createDisplay", "params": { "client": "test-0", "displayName": "test-0" }}'; echo ""
curl 'http://127.0.0.1:9998/jsonrpc' -d '{"jsonrpc": "2.0","id": 4,"method": "org.rdk.RDKShell.1.setFocus", "params": { "client": "test-0" }}' ; echo ""
```

> [!NOTE]
> The RDK UI will freeze after executing these focus commands. Please use the console to navigate to `/data/out_cobalt` and launch the application as described below.

### Launch Cobalt (Executable Mode)
Navigate to `/data/out_cobalt` and launch the application using `loader_app` (executable mode does not support launching via the UI):

```
cd /data/out_cobalt
./loader_app
```

### Restoring RDK UI Control
After closing `loader_app`, the UI freezes because control focus remains with the custom display layer. To restore RDK UI control, run:

```
curl 'http://127.0.0.1:9998/jsonrpc' -d '{"jsonrpc": "2.0","id": 4,"method": "org.rdk.RDKShell.1.setFocus", "params": { "client": "residentapp" }}' ; echo ""
```

## Launch NPLB with elf_loader_sandbox

The `elf_loader_sandbox` is a lightweight loader compared to `loader_app`. Use it to load NPLB or other libraries by providing paths via the `--evergreen_library` and `--evergreen_content` command-line switches.

### Directory Structure (Visual Reference)

When you extract `archive.tar.gz`, the directory structure in `/data/out_cobalt/` will look like this:

```text
out_cobalt/
├── app/
│   ├── cobalt/
│   │   ├── content/
│   │   │   ├── cobalt_shell.pak
│   │   │   ├── etc/
│   │   │   ├── fonts/
│   │   │   └── ssl/
│   │   └── lib/
│   │       └── libcobalt.lz4
│   └── nplb/
├── cobalt_loader.py
├── cobalt_shell.pak
├── cobalt_shell.pak.info
├── elf_loader_sandbox
├── fonts/
├── gen/
├── libcobalt.so
├── libloader_app.so
├── libnplb.so
├── loader_app
├── native_target/
├── nplb_loader.py
├── pyproto/
├── ssl/
└── test/
    └── starboard/shared/starboard/player/testdata/ (contains media .dmp files for player/media tests)
```

### Running NPLB

To run NPLB with a filter and output the result to an XML file, use the following command:

```
cd /data/out_cobalt/
./elf_loader_sandbox \
  --evergreen_content=. \
  --evergreen_library=libnplb.so
```

### Using gtest arguments

You can pass standard Google Test (gtest) arguments.

**Output test result as XML file:**

```
./elf_loader_sandbox \
  --evergreen_library=libnplb.so \
  --evergreen_content=. \
  --gtest_output=xml:/data/nplb_testResult.xml
```

**Run test cases by filter:**

```
./elf_loader_sandbox \
  --evergreen_library=libnplb.so \
  --evergreen_content=. \
  --gtest_filter=*Posix* \
  --gtest_output=xml:/data/nplb_testResult.xml
```
