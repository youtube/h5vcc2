# Cobalt Evergreen Overview

![Cobalt non-Evergreen vs
Evergreen](resources/cobalt_evergreen_overview_flow.png)

## What is Cobalt Evergreen?

Cobalt Evergreen is an end-to-end framework for cloud-based deployment of Cobalt
updates without the need for supplemental Cobalt integration work on device
platforms.

There are two configurations available:
*   Evergreen-Lite
    *   Please read this document for general Evergreen details then see
        Evergreen-Lite specific configuration details in
        [cobalt_evergreen_lite.md](cobalt_evergreen_lite.md)
*   Evergreen Full
    *   Please continue reading below documentation for configuration details

![Cobalt Evergreen Configurations](resources/cobalt_evergreen_configurations.png)

For a bit of background context, as the number of Cobalt devices in the field
increases there is a growing proliferation of version fragmentation. Many of
these devices are unable to take advantage of the benefits of Cobalt
performance, security, and functional improvements as it is costly to integrate
and port new versions of Cobalt. We recognized this issue, listened to feedback
from the Cobalt community and as a result developed Cobalt Evergreen as a way to
make updating Cobalt a much simpler process for everyone involved.

This relies on separating the Starboard(platform) and Cobalt(core) components of
a Cobalt implementation into the following discrete components:

**Google-built** (on Google toolchain)

*   Cobalt Core
    *   Pre-built shared library available for all supported architectures
*   Cobalt Updater
    *   Part of Cobalt Core and used to query servers to check for and download
        updated Cobalt Core

**Partner-built** (on Partner toolchain)

*   Starboard
    *   Platform-specific implementation
    *   Contains system dependencies (e.g. `libpthread.so`, `libEGL.so`)
*   Cobalt Loader (Loader App)
    *   Selects the appropriate Cobalt core for usage
    *   An ELF loader is used to load the Cobalt core and resolves symbols with
        Starboard APIs when Cobalt starts up in Evergreen mode

With this new Cobalt platform architecture, less engineering effort is necessary
for a full Cobalt integration/deployment. The idea here is you should only need
to implement Starboard one time and any Cobalt-level updates should only require
platform testing. NOTE that certain new Cobalt features may require Starboard
changes, so if you want to take advantage of some of these new features,
Starboard changes may be necessary.

### Main Benefits

*   More stable platform as there is less Cobalt version fragmentation
*   Little-to-no engineering effort necessary for Cobalt updates
*   Longer device lifetime due to more Cobalt updates
*   Less engineering work/accelerated timeline for Cobalt integration/deployment
    as Google builds the Cobalt components and partners are only responsible for
    the Starboard, `loader_app`, and `crashpad_handler` portion

### New in Evergreen

*   Larger storage and system permissions requirements in order to update and
    store multiple Cobalt binaries
*   Access permissions to download binaries onto a device platform from Google
    servers
*   New `loader_app` and `crashpad_handler` components required to be built on
    platform toolchains
*   Additional testing/verification required to ensure new Cobalt releases work
    properly

## How is Evergreen different from porting Cobalt previously?

There are minimal differences in switching to Evergreen as the Cobalt team has
already done a majority of the work building the necessary components to support
the Evergreen architecture. You will still be responsible for building the
Starboard and platform-specific components as usual. Thereafter, switching to
Evergreen is as simple as building a different configuration.

![Cobalt non-Evergreen vs
Evergreen](resources/cobalt_evergreen_overview_vs_non_evergreen.png)

### Building Cobalt Evergreen Components

Because of its separation of the Cobalt core (`libcobalt.so`) and platform-
specific Starboard layers, Cobalt Evergreen requires two build configurations.
This means there are two relevant GN platforms defined in the build code, and
that `gn gen` must be run twice to generate ninja files for two separate
platforms.

#### Configuring your port for Evergreen

The Evergreen platforms for the different supported architectures, which are
used to build Cobalt core (`libcobalt.so`), are maintained by Google. So, no
changes are needed by the partner here.

However, a few small changes are needed in the partner's port, which is used to
build the partner-built components, to make it compatible with Evergreen.

First, ensure that your platform is built with Starboard enabled (`is_starboard = true`, which is the default for all Starboard platforms). Please DO NOT set `sb_is_evergreen` to `true`, as this should only be set in the Evergreen platforms that are maintained by Google and used to build Cobalt core.

Second, in the platform's `toolchain/BUILD.gn` file partners should copy their
"starboard" toolchain to add a "native_target" toolchain that is identical
except that it sets `is_starboard = false`.

For example:

```
gcc_toolchain("target") {
  ...
}

gcc_toolchain("native_target") {
  ...
  is_starboard = false
}
```

This "native_target" toolchain is required in order to build the
`crashpad_handler` binary, which should NOT use the Starboard porting layer.

Additionally, partners should install crash handlers as instructed in the
[Installing Crash Handlers for Cobalt guide](../crash_handlers.md).

The following additional Starboard interfaces are necessary to implement for
Evergreen:

*   `kSbSystemPathStorageDirectory`
    *   Dedicated location for storing Cobalt Evergreen-related binaries
    *   This path must be writable and have at least 64MB of reserved space for
        Evergreen updates. Please see the “Platforms Requirements” section below
        for more details.
*   `kSbMemoryMapProtectExec`
    *   Ensures mapped memory can be executed
*   Set `kSbCanMapExecutableMemory` to `true`
    *   Specifies that the platform can map executable memory
    *   Defined in `configuration_constants.h`

Only if necessary, create a customized SABI configuration for your architecture.
Note, we do not anticipate that you will need to make a new configuration for
your platform unless it is not one of our supported architectures:

*   x86\_64
*   arm32
*   arm64

If your target architecture falls outside the support list above, please reach
out to us for guidance.

#### Build commands

As mentioned, the Google-maintained Evergreen toolchain is used to build Cobalt
core (`libcobalt.so`). For example:

```
$ gn gen out/evergreen-arm-softfp_qa --args="target_platform=\"evergreen-arm-softfp\" build_type=\"qa\" target_cpu=\"arm\" target_os=\"linux\" sb_api_version=15"
$ ninja -C out/evergreen-arm-softfp_qa cobalt_install
```

This produces a `libcobalt.so` shared library, and a compressed `libcobalt.lz4`
copy, targeted for a specific architecture, ABI, and Starboard version.

Note: `sb_api_version` defaults to the latest supported Starboard version in the
current branch.


The partner port of Starboard is built with the partner’s "target" toolchain and
is linked into the `loader_app`, which knows how to dynamically load
`libcobalt.lz4`. And the `crashpad_handler` binary is built with the partner's
"native_target" toolchain. For example:

```
$ gn gen out/<partner_port_name>_qa --args='target_platform="<partner_port_name>" build_type="qa" sb_api_version=15'
$ ninja -C out/<partner_port_name>_qa loader_app
$ ninja -C out/<partner_port_name>_qa native_target/crashpad_handler
```

Note that when building `crashpad_handler`, a special prefix is used to have
Ninja compile the target in the non-default, "native_target" toolchain.

### What is an example for how this would help me?

Some common versions of Cobalt in the field may show a bug in the implementation
of the CSS which can cause layout behavior to cause components to overlap and
give users a poor user experience. A fix for this is identified and pushed to
Cobalt open source ready for integration and deployment on devices.

#### Without Cobalt Evergreen:

Though a fix for this was made available in the latest Cobalt open source,
affected devices in the field are not getting updated (e.g. due to engineering
resources, timing, device end-of-life), users continue to have a poor experience
and have negative sentiment against a device. In parallel, the web app team
determines a workaround for this particular situation, but the workaround is
obtuse and causes app bloat and technical debt from on-going maintenance of
workarounds for various Cobalt versions.

#### With Cobalt Evergreen:

The Cobalt team can work with you to guide validation and deployment of a shared
Cobalt library to all affected devices much more quickly without all the
engineering effort required to deploy a new Cobalt build. With this simpler
updating capability, device behavior will be more consistent and there is less
technical debt from workarounds on the web application side. Additionally, users
can benefit from the latest performance, security, and functional fixes.

## Platform Requirements

Cobalt Evergreen currently supports the following

Target Architectures:

*   x86\_64
*   armv7 32
*   armv8 64

Supported Javascript Engines

*   V8

Additional reserved storage (64MB) is required for Evergreen binaries. We expect
Evergreen implementations to have an initial Cobalt preloaded on the device and
an additional reserved space for additional Cobalt update storage.

*   Initial Cobalt binary deployment - 64MB
*   Additional Cobalt update storage - 64MB
    *   Required for 2 update slots under `kSbSystemPathStorageDirectory`

As Cobalt Evergreen is intended to be updated from Google Cloud architecture
without the need for device FW updates, it is important that this can be done
easily and securely on the target platform. There are a set of general minimum
requirements to do so:

*   Writable access to the file system of a device platform to download Cobalt
    Evergreen binaries
*   Enough reserved storage for Cobalt updates
*   Platform supporting mmap API with writable memory (`PROT_WRITE`,
    `PROT_EXEC`) for loading in-memory and performing relocations for Cobalt
    Evergreen binaries

## Building and Running Tests

The `elf_loader_sandbox` binary can be used to run tests in Evergreen mode. This
is much more lightweight than the `loader_app`, and does not have any knowledge
about installations or downloading updates.

The `elf_loader_sandbox` is run using two command line switches:
`--evergreen_library` and `--evergreen_content`. These switches are the path to
the shared library to be run and the path to that shared library's content.
These paths should be *relative to the content of the elf_loader_sandbox*.

For example, if we wanted to run the NPLB set of tests and had the following
directory tree,

```
.../elf_loader_sandbox
.../content/app/nplb/lib/libnplb.so
.../content/app/nplb/content
```

we would use the following command to run NPLB:

```
.../elf_loader_sandbox --evergreen_library=app/nplb/lib/libnplb.so
                       --evergreen_content=app/nplb/content
```

Building tests is identical to how they are already built except that a
different platform configuration must be used. The platform configuration should
be an Evergreen platform configuration, and have a Starboard ABI file that
matches the file used by the platform configuration used to build the
`elf_loader_sandbox`.

For example, building these targets for the RDK would use the
`rdk-arm` and `evergreen-arm-hardfp-rdk` platform configurations.

## Verifying Platform Requirements

In order to verify the platform requirements you should run the
`nplb_evergreen_compat_tests`. These tests ensure that the platform is
configured appropriately for Evergreen.

These tests are enabled automatically for all Starboard platforms (`is_starboard = true`).

### Verifying Crashpad Uploads

1. Build the `crashpad_database_util` target and deploy it onto the device.
```
$ gn gen out/<partner_port_name>_qa --args='target_platform="<partner_port_name>" build_type="qa"'
$ ninja -C out/<partner_port_name>_qa native_target/crashpad_database_util
```
2. Remove the existing state for crashpad as it throttles uploads to 1 per hour:
```
$ rm -rf <kSbSystemPathCacheDirectory>/crashpad_database/

```
3. Launch Cobalt.
4. Trigger crash by sending `abort` signal to the `loader_app` process:
```
$ kill -6 <pid>
```
5. Verify the crash was uploaded through running `crashpad_database_util` on the device
pointing it to the cache directory, where the crash data is stored.

```
$ crashpad_database_util -d <kSbSystemPathCacheDirectory>/crashpad_database/ --show-completed-reports --show-all-report-info
```

```
8c3af145-30a0-43c7-a3a5-0952dea230e4:
  Path: cobalt/cache/crashpad_database/completed/8c3af145-30a0-43c7-a3a5-0952dea230e4.dmp
  Remote ID: c9b14b489a895093
  Creation time: 2021-06-01 17:01:19 HDT
  Uploaded: true
  Last upload attempt time: 2021-06-01 17:01:19 HDT
  Upload attempts: 1
```

In this example the minidump was successfully uploaded because we see `Uploaded: true`.

Reference for [crashpad_database_util](https://chromium.googlesource.com/crashpad/crashpad/+/refs/heads/main/tools/crashpad_database_util.md)

## System Design

![Cobalt Evergreen
Components](resources/cobalt_evergreen_overview_components.png)

The above diagram is a high-level overview of the components in the Cobalt
Evergreen architecture.

* **Partner-built** represents components the Partner is responsible for
  implementing and building.

* **Cobalt-built** represents components the Cobalt team is responsible for
  implementing and building.

### Cobalt Evergreen Components

#### Cobalt Updater

This is a new component in the Cobalt Shared Library component that is built on
top of the Starboard API. The purpose of this module is to check the update
servers if there is a new version of the Cobalt Shared Library available for the
target device. If a new version is available, the Cobalt updater will download,
verify, and install the new package on the target platform. The new package can
be used the next time Cobalt is started or it can be forced to update
immediately and restart Cobalt as soon as the new package is available and
verified on the platform. This behavior will take into account the
suspend/resume logic on the target platform.

Functionally, the Cobalt Updater itself runs as a separate thread within the
Cobalt process when Cobalt is running. This behavior depends on what the target
platform allows.

For more detailed information on Cobalt Updater, please take a look
[here](cobalt_update_framework.md).

#### Google Update (Update Server)

We use Google Update as the infrastructure to manage the Cobalt Evergreen
package install and update process. This has been heavily used across Google for
quite some time and has the level of reliability required for Cobalt Evergreen.
There are also other important features such as:

*   Fine grained device targeting
*   Rollout and rollback
*   Multiple update channels (e.g. production, testing, development)
*   Staged rollouts

For more detailed information on Google Update for Cobalt Evergreen, please take
a look [here](cobalt_update_framework.md).

#### Google Downloads (Download Server)

We use Google Downloads to manage the downloads available for Cobalt Evergreen.
The Cobalt Updater will use Google Downloads in order to download available
packages onto the target platform. We selected Google Downloads for this purpose
due to its ability to scale across billions of devices as well as the
flexibility to control download behavior for reliability.

For more detailed information on Google Downloads (Download Server) for Cobalt
Evergreen, please take a look [here](cobalt_update_framework.md).

### Cobalt Evergreen Interfaces

#### Starboard ABI

The Starboard ABI was introduced to provide a single, consistent method for
specifying the Starboard API version and the ABI. This specification ensures
that any two binaries, built with the same Starboard ABI and with arbitrary
toolchains, are compatible.

Note that Cobalt already provides default SABI files for the following
architectures:

*   x86\_64
*   arm v7 (32-bit)
*   arm v8 (64-bit)

You should not need to make a new SABI file for your target platform unless it
is not a currently supported architecture. We recommend that you do not make any
SABI file changes. If you believe it is necessary to create a new SABI file for
your target platform, please reach out to the Cobalt team to advise.

For more detailed information on the Starboard ABI for Cobalt Evergreen, please
take a look here.

### Installation Slots

Cobalt Evergreen provides support for maintaining multiple, separate versions of
the Cobalt binary on a platform. These versions are stored in installation
slots(i.e. known locations on disk), and are used to significantly improve the
resilience and reliability of Cobalt updates.

All slot configurations assume the following:
* 1 System Image Installation Slot (read-only)
* 2+ Additional Installation Slot(s) (writable)

The number of installation slots available will be determined by the platform
owner. **3 slots is the default configuration for Evergreen**. There can be `N`
installation slots configured with the only limitation being available storage.

#### Slot Configuration
NOTE: 3-slots is the DEFAULT configuration.

The number of installation slots is directly controlled using
`kMaxNumInstallations`, defined in
[loader\_app.cc](../../loader_app/loader_app.cc).

It is worth noting that all slot configurations specify that the first
installation slot (`SLOT_0`) will always be the read-only factory system image.
This is permanently installed on the platform and is used as a fail-safe option.
This is stored in the directory specified by `kSbSystemPathContentDirectory`
under the `app/cobalt` subdirectory.

All of the other installation slots are located within the storage directory
specified by `kSbSystemPathStorageDirectory`. This will vary depending on the
platform.

For example, on the Raspberry Pi the `kSbSystemPathStorageDirectory` directory
is `/home/pi/.cobalt_storage`, and the paths to all existing installation slots
will be as follows:

```
/home/pi/<kSbSystemPathContentDirectory>/app/cobalt (system image installation SLOT_0) (read-only)
/home/pi/.cobalt_storage/installation_1 (SLOT_1)
/home/pi/.cobalt_storage/installation_2 (SLOT_2)
...
/home/pi/.cobalt_storage/installation_N (SLOT_N)
```

Where the most recent update is stored will alternate between the available
writable slots. In the above example, this would be `SLOT_1`...`SLOT_N`.

#### Understanding Slot Structure
Slots are used to manage Cobalt Evergreen binaries with associated app metadata
to select the appropriate Cobalt Evergreen binaries.

See the below structures for an example 3-slot configuration.

Structure for `kSbSystemPathContentDirectory` used for the read-only System
Image required for all slot configurations:

```
.
├── content <--(kSbSystemPathContentDirectory)
│   └── fonts <--(kSbSystemPathFontDirectory, `standard` or `limit` configuration, to be explained below)
│   └── app
│       └── cobalt <--(SLOT_0)
│           ├── content <--(relative path defined in kSystemImageContentPath)
│           │   ├── fonts <--(`empty` configuration)
│           │   ├── licenses
│           │   ├── ssl
│           ├── lib
│           │   └── libcobalt.lz4 <--(System image version of Cobalt Core)
│           └── manifest.json
└── loader_app <--(Cobalt launcher binary)
└── crashpad_handler <--(Cobalt crash handler)
```

Structure for `kSbSystemPathStorageDirectory` used for future Cobalt Evergreen
updates in an example 3-slot configuration:

```
├── .cobalt_storage <--(kSbSystemPathStorageDirectory)
    ├── cobalt_updater
    │   └── prefs_<APP_KEY>.json
    ├── installation_1 <--(SLOT_1 - currently unused)
    ├── installation_2 <--(SLOT_2 - contains new Cobalt version)
    │   ├── content
    │   │   ├── fonts <--(`empty` configuration)
    │   │   ├── licenses
    │   │   ├── ssl
    │   ├── lib
    │   │   └── libcobalt.lz4 <--(SLOT_2 version of Cobalt Core)
    │   ├── manifest.fingerprint
    │   └── manifest.json <-- (Evergreen version information of Cobalt Core under SLOT_2)
    ├── installation_store_<APP_KEY>.pb
```
Note that after the Cobalt binary is loaded by the loader_app, `kSbSystemPathContentDirectory` points to the
content directory of the running binary, as stated in Starboard Module Reference of system.h.

#### App metadata
Each Cobalt Evergreen application has a set of unique metadata to track slot
selection. The following set of files are unique per application via a
differentiating <APP_KEY> identifier, which is a Base64 hash appended to the
filename.

```
<SLOT_#>/installation_store_<APP_KEY>.pb
<SLOT_#>/cobalt_updater/prefs_<APP_KEY>.json
```

You should NOT change any of these files and they are highlighted here just for
reference.


### Fonts
The system font directory `kSbSystemPathFontDirectory` should be configured to
point to either the system fonts on the device or the Cobalt `standard` (23MB)
or the Cobalt `limited` (3.1MB) font packages. An easy way to use the Cobalt
fonts is to set `kSbSystemPathFontDirectory` to point to
`kSbSystemPathContentDirectory/fonts` and configure `cobalt_font_package` to
`standard` or `limited` in your port.

Cobalt Evergreen (built by Google), will by default use the `empty` font
package to minimize storage requirements. A separate
`cobalt_font_package` variable is set to `empty` in the Evergreen platform.

On Raspberry Pi the Cobalt fonts are configured the following way:

`empty` set of fonts under:
```
<kSbSystemPathContentDirectory>/app/starboard/content/fonts
```

`standard` or `limited` set of fonts under:
```
<kSbSystemPathContentDirectory>/fonts
```

### Handling Pending Updates
Pending updates will be picked up on the next application start, which means
that on platforms that support suspending the platform should check
`loader_app::IsPendingRestart` and call `SbSystemRequestStop` instead of
 suspending if there is a pending restart.

Please see
[`suspend_signals.cc`](../../shared/signal/suspend_signals.cc)
for an example.

### Cleaning up after uninstall
When the application is uninstalled the updates should be cleanup by calling the
application with the `--reset_evergreen_update` flag. This would remove all files
under `kSbSystemPathStorageDirectory` and exit the app.

```
  loader_app --reset_evergreen_update
```

### Multi-App Support
Evergreen can support multiple apps that share a Cobalt binary. This is a very
common way to save space and keep all your Cobalt apps using the latest version
of Cobalt. We understand that there are situations where updates are only needed
for certain apps, so we have provided a way where Cobalt Updater and loader_app
behavior can be easily configured on a per-app basis with simple command-line flags.

The configurable options for Cobalt Updater configuration are:
* `--evergreen_lite` *Use the System Image version of Cobalt under Slot_0 and turn
  off the updater for the specified application.*

Each app’s Cobalt Updater will perform an independent, regular check for new
Cobalt Evergreen updates. Note that all apps will share the same set of slots,
but each app will maintain metadata about which slots are “good” (working) or
“bad” (error detected) and use the appropriate slot. Sharing slots allows
Evergreen to download Cobalt updates a single time and be able to use it across
all Evergreen-enabled apps.

To illustrate, a simple example:

* Cobalt v5 - latest Cobalt Evergreen version

#### BEFORE COBALT UPDATE
```
[APP_1] (currently using SLOT_1, using Cobalt v4)
[APP_2] (currently using SLOT_0, using Cobalt v3)
[APP_3] (currently using SLOT_0, using Cobalt v3)
```

Now remember, apps could share the same Cobalt binary. Let’s say `APP_1` has
detected an update available and downloads the latest update (Cobalt v5) into
SLOT_2. The next time `APP_2` runs, it may detect Cobalt v5 as well. It would
then simply do a `request_roll_forward` operation to switch to SLOT_2 and does
not have to download a new update since the latest is already available in an
existing slot. In this case, `APP_1` and `APP_2` are now using the same Cobalt
binaries in SLOT_2.

If `APP_3` has not been launched, not run through a regular Cobalt Updater
check, or launched with the `--evergreen_lite` flag,
it stays with its current configuration.

#### AFTER COBALT UPDATE
```
[APP_1] (currently using SLOT_2, using Cobalt v5)
[APP_2] (currently using SLOT_2, using Cobalt v5)
[APP_3] (currently using SLOT_0, using Cobalt v3)
```

Now that we have gone through an example scenario, we can cover some examples of
how to configure Cobalt Updater behavior and `loader_app` configuration.


Some example configurations include:
```

# All Cobalt-based apps get Evergreen Updates
[APP_1] (Cobalt Updater ENABLED)
[APP_2] (Cobalt Updater ENABLED)
[APP_3] (Cobalt Updater ENABLED)

loader_app --url="<YOUR_APP_1_URL>"
loader_app --url="<YOUR_APP_2_URL>"
loader_app --url="<YOUR_APP_3_URL>"


# APP_1 gets Evergreen Updates, APP_2 uses an alternate splash screen, APP_3 uses
# the system image and disables the updater
[APP_1] (Cobalt Updater ENABLED)
[APP_2] (Cobalt Updater ENABLED)
[APP_3] (System Image loaded, Cobalt Updater DISABLED)

loader_app --url="<YOUR_APP_1_URL>"
loader_app --url="<YOUR_APP_2_URL>" --fallback_splash_screen_url="/<PATH_TO_APP_2>/app_2_splash_screen.html"
loader_app --url="<YOUR_APP_3_URL>" --evergreen_lite


# APP_3 is a local app, wants Cobalt Updater disabled and stays on the system image, and uses an alternate content
# directory (This configuration is common for System UI apps. APP_3 in this example.)
[APP_1] (Cobalt Updater ENABLED)
[APP_2] (Cobalt Updater ENABLED)
[APP_3] (System Image loaded, Cobalt Updater DISABLED)

loader_app --url="<YOUR_APP_1_URL>"
loader_app --url="<YOUR_APP_2_URL>"
loader_app --csp_mode=disable --allow_http --url="file:///<PATH_TO_APP_3>/index.html" --content="/<PATH_TO_APP_3>/content" --evergreen_lite
```

Please see
[`loader_app_switches.cc`](../../loader_app/loader_app.cc)
for full list of available command-line flags.

### Platform Security

As Cobalt binary packages ([CRX
format](https://docs.google.com/document/d/1pAVB4y5EBhqufLshWMcvbQ5velk0yMGl5ynqiorTCG4/edit#heading=h.ke61kmpkapku))
are downloaded from the Google Downloads server, the verification of the Cobalt
update package is critical to the reliability of Cobalt Evergreen. There are
mechanisms in place to ensure that the binary is verified and a chain of trust
is formed. The Cobalt Updater is responsible for downloading the available
Cobalt update package and verifies that the package is authored by Cobalt(and
not an imposter), before trying to install the downloaded package.

#### Understanding Verification

![Cobalt Evergreen CRX
Verification](resources/cobalt_evergreen_overview_crx_verification.png)

In the above diagram, the Cobalt Updater downloads the update package if
available, and parses the CRX header of the package for verification, before
unpacking the whole package. A copy of the Cobalt public key is contained in the
CRX header, so the updater retrieves the key and generates the hash of the key
coming from the header of the package, say _Key_ _hash1_.

At the same time, the updater has the hash of the Cobalt public key hard-coded
locally, say _Key hash2_.

The updater compares _Key hash1_ with _Key hash2._ If they match, verification
succeeds.

## **FAQ**

### Can I host the binaries for Cobalt core myself to deploy on my devices?

Not at this time. All Cobalt updates will be deployed through Google
infrastructure. We believe Google hosting the Cobalt core binaries allows us to
ensure a high-level of reliability and monitoring in case issues arise.

### What is the performance impact of switching to Cobalt Evergreen?

We expect performance to be similar to a standard non-Evergreen Cobalt port.

### How can I ensure that Cobalt updates work well on our platform?

Google will work closely with device partners to ensure that the appropriate
testing is in place to prevent regressions.

### Will there be tests provided to verify functionality and detect regression?

Yes, there are tests available to help validate the implementation:

*   NPLB tests to ensure all necessary APIs are implemented
*   Cobalt Evergreen Test Plan to verify the functionality of all components and
    use cases

### How can I be sure that Cobalt space requirements will not grow too large for my system resources?

The Cobalt team is focusing a large amount of effort to identify and integrate
various methods to reduce the size of the Cobalt binary such as compression and
using less fonts.

### What if something goes wrong in the field? Can we rollback?

Yes, this is one of the benefits of Evergreen. We can initiate an update from
the server side that addresses problems that were undetected during full
testing. There are a formal set of guidelines to verify an updated binary
deployed to the device to ensure that it will work properly with no regressions
that partners should follow to ensure that there are no regressions. In
addition, it is also critical to do your own testing to exercise
platform-specific behavior.

### How can I be sure that Cobalt Evergreen will be optimized for my platform?

Much of the optimization work remains in the Starboard layer and configuration
so you should still expect good performance using Cobalt Evergreen. That being
said, the Cobalt Evergreen configuration allows you to customize Cobalt features
and settings as before.
