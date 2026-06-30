Project: /youtube/cobalt/_project.yaml
Book: /youtube/cobalt/_book.yaml

# Starboard Extensions codelab

The Starboard Extension framework allows you to add optional, platform-specific
features to Starboard applications. Porters can implement these optional
interfaces for their platforms as needed.

This tutorial uses coding exercises to guide you through creating a simple
Starboard Extension.

By the end, you will learn:
* What Starboard Extensions are and when to use them.
* How to write Starboard Extensions.
* How to collaborate with the Cobalt team to contribute your extensions to the repository.

## Prerequisites

To build and run Cobalt during the exercises, set up your development
environment first. See the setup guides for
[Linux](../../development/setup-linux.md),
[RDK](../../development/setup-rdk.md), and
[Android](../../development/setup-android.md) developers
for details. Although the exercise solutions assume a Linux environment, the
implementation steps are similar for other platforms.

Although this codelab does not require it, you must
[port Cobalt to your platform](../../starboard/porting.md) before using a
Starboard Extension for customization.

Finally, the exercises assume the ability to program in C and C++.

### Exercise 0: Run Cobalt and inspect logs

Assuming you have built Cobalt, run it and note the message logged at startup.
This message is the focus of subsequent exercises.

```
$ out/linux-x64x11_debug/cobalt 2>/dev/null | grep "Starting application"
```

## Background

Starboard sits below Cobalt. As a porting layer and OS abstraction, Starboard
contains a minimal set of APIs that encapsulate platform-specific functionality.
Each Starboard module (such as memory, socket, or thread) defines functions
that you must implement for your platform, which requires implementation and
maintenance effort. To minimize these costs, the Cobalt team keeps Starboard
APIs stable by adding a new API version only when Cobalt requires functionality
that depends on the platform.

To make a Starboard API optional, introduce a query API. The query API lets
Cobalt check for feature support at runtime. For example,
`SbWindowOnScreenKeyboardIsSupported` allows platforms without on-screen
keyboard support to skip implementing the related functions in
`starboard/window.h`. The Cobalt team makes a Starboard API optional when
the associated Cobalt feature is optional and its implementation is
platform-dependent.

Additionally, other applications besides Cobalt can run on top of Starboard.
If only Cobalt needs a feature, adding it to the Starboard API introduces
unnecessary complexity for other Starboard-based applications and unnecessary
size to the porting layer.

Starboard Extensions are most useful in such cases where the desired
functionality is Cobalt-specific, optional in Cobalt, and platform-dependent.
Because Starboard Extensions are lightweight and do not modify the Starboard
layer, they are the preferred way to add custom features to Cobalt.

To summarize:

<table>
  <tr>
    <th colspan="1">Tool</th>
    <th colspan="1">Use case</th>
    <th colspan="1">Ecosystem cost</th>
  </tr>
  <tr>
    <td>Starboard API</td>
    <td>Feature is <strong>required</strong> but implementation is
    platform-dependent</td>
    <td>High</td>
  </tr>
  <tr>
    <td>Optional Starboard API</td>
    <td>Feature is <strong>optional</strong> and implementation is
    platform-dependent</td>
    <td>Medium</td>
  </tr>
  <tr>
    <td>Starboard Extension</td>
    <td>Feature is <strong>optional and specific to Cobalt</strong> and
    implementation is platform-dependent</td>
    <td>Low</td>
  </tr>
</table>

Note that for all three abstractions, the interface is public in Cobalt's
open-source repository. The implementation, however, is built by porters and
can remain private.

Porters sometimes make local changes to Cobalt above the Starboard layer for
customization or optimization. The Cobalt team discourages this because it
complicates rebasing. However, it was historically possible because porters
built both Cobalt and Starboard.

With Cobalt Evergreen, local changes are no longer possible. Evergreen
separates the Google-built Cobalt core shared library from the partner-built
Starboard layer and loader app. Because Google builds the Cobalt core, partners
using Evergreen cannot make custom changes to it.

## Anatomy of a Starboard Extension

### Extension structures

Cobalt describes extension interfaces using structures, which it organizes in
header files under `starboard/extension/`. For example, the header for a "foo"
extension should be named `foo.h`. The initial version should contain the
following template, along with any members that provide the "foo"
functionality:

```
#ifndef STARBOARD_EXTENSION_FOO_H_
#define STARBOARD_EXTENSION_FOO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kStarboardExtensionFooName "dev.starboard.extension.Foo"

typedef struct StarboardExtensionFooApi {
  // Name should be the string |kStarboardExtensionFooName|.
  // This helps to validate that the extension API is correct.
  const char* name;

  // This specifies the version of the API that is implemented.
  uint32_t version;

  // The fields below this point were added in version 1 or later.

} StarboardExtensionFooApi;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // STARBOARD_EXTENSION_FOO_H_
```

Note the following points about this structure:

*   The first two members must be, in order:
    *   A `const char* |name|` that stores the extension's name.
    *   A `uint32_t |version|` that stores the extension's version number.
        (Extension versioning is discussed later.)
*   The remaining members can be any useful C types, including custom
    structures. They are often function pointers.

### Extension access in Cobalt

The `SbSystemGetExtension` function from Starboard's `system` module allows
Cobalt to query for extensions by name. If the extension exists, the function
returns a pointer to its constant, global structure instance; otherwise, it
returns `NULL`. This is the only way for Cobalt to access an extension. The
Starboard interface intentionally excludes functions related to specific
extensions.

Before using the extension, Cobalt must cast the `const void*` that
`SbSystemGetExtension` returns to the appropriate pointer type (for example,
`const StarboardExtensionFooApi*`).
Because you cannot guarantee that a platform implements the extension (or
implements it correctly), practice defensive programming: verify that the
returned pointer is not `NULL` and that its `name` member matches
`kStarboardExtensionFooName`.

### Extension implementation

Because Starboard Extensions are platform-dependent, their implementations
belong in Starboard ports. A Starboard port implements an extension by
defining a constant, global instance of the structure and implementing
`SbSystemGetExtension` to return a pointer to it. Here is an example
implementation of the "foo" extension for the `custom_platform` Starboard port:

`starboard/custom_platform/foo.h` declares a `GetFooApi` accessor for the
structure:

```
#ifndef STARBOARD_CUSTOM_PLATFORM_FOO_H_
#define STARBOARD_CUSTOM_PLATFORM_FOO_H_

namespace starboard {
namespace custom_platform {

const void* GetFooApi();

}  // namespace custom_platform
}  // namespace starboard

#endif  // STARBOARD_CUSTOM_PLATFORM_FOO_H_
```

`starboard/custom_platform/foo.cc` defines `GetFooApi`:

```
#include "starboard/custom_platform/foo.h"

#include "starboard/extension/foo.h"

namespace starboard {
namespace custom_platform {

namespace {

// Definitions of any functions included as components in the extension
// are added here.

const StarboardExtensionFooApi kFooApi = {
    kStarboardExtensionFooName,
    1,  // API version that's implemented.
    // Any additional members are initialized here.
};

}  // namespace

const void* GetFooApi() {
  return &kFooApi;
}

}  // namespace custom_platform
}  // namespace starboard
```

Finally, `starboard/custom_platform/system_get_extension.cc` defines
`SbSystemGetExtension` to expose the extension:

```
#include "starboard/system.h"

#include "starboard/extension/foo.h"
#include "starboard/common/string.h"
#include "starboard/custom_platform/foo.h"

const void* SbSystemGetExtension(const char* name) {
  if (strcmp(name, kStarboardExtensionFooName) == 0) {
    return starboard::custom_platform::GetFooApi();
  }
  // Other conditions here should handle other implemented extensions.

  return NULL;
}
```

You can browse existing extension implementations in the repository.
For example, the reference Linux port implements the `Pleasantry` extension
across the following files.

*   `starboard/linux/shared/pleasantry.h`
*   `starboard/linux/shared/pleasantry.cc`
*   `starboard/linux/shared/system_get_extensions.cc`

### Exercise 1: Write and use your first extension

Now, write your own extension. In Exercise 0, you observed that Cobalt logs
"Starting application" at startup.

1.  Create a `Pleasantry` extension with a `const char* greeting` member.
2.  Modify `cobalt/browser/main.cc` to log this custom greeting immediately after
    "Starting application."
3.  Implement the extension for Linux (or your preferred platform).
4.  Verify that the greeting is logged.


#### Solution to Exercise 1

Click the items below to expand parts of a solution. The `git diff`s are between
the solution and the `master` branch.

<details>
    <summary style="display:list-item">Contents of new
    `starboard/extension/pleasantry.h` file.</summary>

```
#ifndef STARBOARD_EXTENSION_PLEASANTRY_H_
#define STARBOARD_EXTENSION_PLEASANTRY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kStarboardExtensionPleasantryName "dev.starboard.extension.Pleasantry"

typedef struct StarboardExtensionPleasantryApi {
  // Name should be the string |kStarboardExtensionPleasantryName|.
  // This helps to validate that the extension API is correct.
  const char* name;

  // This specifies the version of the API that is implemented.
  uint32_t version;

  // The fields below this point were added in version 1 or later.
  const char* greeting;

} StarboardExtensionPleasantryApi;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // STARBOARD_EXTENSION_PLEASANTRY_H_
```

</details>

<details>
    <summary style="display:list-item">Contents of new
    `starboard/linux/shared/pleasantry.h` file.</summary>

```
#ifndef STARBOARD_LINUX_SHARED_PLEASANTRY_H_
#define STARBOARD_LINUX_SHARED_PLEASANTRY_H_

namespace starboard {
namespace shared {

const void* GetPleasantryApi();

}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_LINUX_SHARED_PLEASANTRY_H_
```

</details>

<details>
    <summary style="display:list-item">Contents of new
    `starboard/linux/shared/pleasantry.cc` file.</summary>

```
#include "starboard/linux/shared/pleasantry.h"

#include "starboard/extension/pleasantry.h"

namespace starboard {
namespace shared {

namespace {

const char *kGreeting = "Happy debugging!";

const StarboardExtensionPleasantryApi kPleasantryApi = {
    kStarboardExtensionPleasantryName,
    1,
    kGreeting,
};

}  // namespace

const void* GetPleasantryApi() {
  return &kPleasantryApi;
}

}  // namespace shared
}  // namespace starboard
```

</details>

<details>
    <summary style="display:list-item">`git diff
    starboard/linux/shared/BUILD.gn`</summary>

```
@@ -71,6 +71,8 @@ static_library("starboard_platform_sources") {
     "//starboard/linux/shared/netlink.cc",
     "//starboard/linux/shared/netlink.h",
     "//starboard/linux/shared/player_components_factory.cc",
+    "//starboard/linux/shared/pleasantry.cc",
+    "//starboard/linux/shared/pleasantry.h",
     "//starboard/linux/shared/routes.cc",
     "//starboard/linux/shared/routes.h",
     "//starboard/linux/shared/soft_mic_platform_service.cc",
```

</details>

<details>
    <summary style="display:list-item">`git diff
    starboard/linux/shared/system_get_extensions.cc`</summary>

```
@@ -22,6 +22,7 @@
 #include "starboard/extension/free_space.h"
 #include "starboard/extension/memory_mapped_file.h"
 #include "starboard/extension/platform_service.h"
+#include "starboard/extension/pleasantry.h"
 #include "starboard/linux/shared/soft_mic_platform_service.h"
 #include "starboard/shared/enhanced_audio/enhanced_audio.h"
 #include "starboard/shared/ffmpeg/ffmpeg_demuxer.h"
@@ -33,6 +34,7 @@
 #include "starboard/elf_loader/evergreen_config.h"
 #endif
 #include "starboard/linux/shared/configuration.h"
+#include "starboard/linux/shared/pleasantry.h"

 const void* SbSystemGetExtension(const char* name) {
 #if BUILDFLAG(IS_STARBOARD)
@@ -74,5 +76,8 @@ const void* SbSystemGetExtension(const char* name) {
     return use_ffmpeg_demuxer ? starboard::shared::ffmpeg::GetFFmpegDemuxerApi()
                               : NULL;
   }
+  if (strcmp(name, kStarboardExtensionPleasantryName) == 0) {
+    return starboard::shared::GetPleasantryApi();
+  }
   return NULL;
 }

```

</details>

<details>
    <summary style="display:list-item">`git diff cobalt/browser/main.cc`
    </summary>

```
@@ -19,6 +19,8 @@
 #include "cobalt/browser/application.h"
 #include "cobalt/browser/switches.h"
 #include "cobalt/version.h"
+#include "starboard/extension/pleasantry.h"
+#include "starboard/system.h"

 namespace {

@@ -77,6 +79,14 @@ void StartApplication(int argc, char** argv, const char* link,
     return;
   }
   LOG(INFO) << "Starting application.";
+  const StarboardExtensionPleasantryApi* pleasantry_extension =
+      static_cast<const StarboardExtensionPleasantryApi*>(
+          SbSystemGetExtension(kStarboardExtensionPleasantryName));
+  if (pleasantry_extension &&
+      strcmp(pleasantry_extension->name, kStarboardExtensionPleasantryName) == 0 &&
+      pleasantry_extension->version >= 1) {
+    LOG(INFO) << pleasantry_extension->greeting;
+  }
 #if SB_API_VERSION >= 13
   DCHECK(!g_application);
   g_application = new cobalt::browser::Application(quit_closure,
```

</details>

## Extension versioning

Starboard Extensions are extensible. However, you must ensure that the
extension interface in Cobalt remains consistent with the implementation in the
platform's port, which may be built separately.

The `version` member, which is always the second member in an extension
structure, indicates the interface version described by the structure. Each
version corresponds to a specific, invariant list of members. By convention,
the first version of a Cobalt Extension is version `1` (one-based indexing).

A new version of the extension can be introduced by appending members to the
structure declaration. Add a comment to delineate the changes (for example,
"The fields below this point were added in version 2 or later").

To maintain compatibility and ensure Cobalt indexes structure instances
correctly, always add members to the end of the declaration and never remove
existing members. If you deprecate a member in a later version, note this in a
comment within the structure declaration.

To implement a new version of the extension, the platform's port must set the
`version` member to the appropriate value when instantiating the structure, and
initialize all members required for that version.

Finally, any Cobalt code using the extension must guard references to
version-specific members with version checks.

### Exercise 2: Version your extension

Add a second version of the `Pleasantry` extension that allows porters to log a
polite farewell message when Cobalt stops. To provide flexibility, add the new
`farewell` member as a pointer to a function that takes no parameters and
returns a `const char*`.

Update `cobalt/browser/main.cc` so that if the platform implements version 2,
Cobalt replaces the "Stopping application" message with the platform's
farewell message.

Configure the platform's implementation to return one of several farewell
messages pseudo-randomly. After making these changes, build and run Cobalt to
verify the behavior.

#### Solution to Exercise 2

Click the items below to expand parts of a solution. The `git diff` is between
the solution and the `master` branch.

<details>
    <summary style="display:list-item">Updated contents of
    `starboard/extension/pleasantry.h`.</summary>

```
#ifndef STARBOARD_EXTENSION_PLEASANTRY_H_
#define STARBOARD_EXTENSION_PLEASANTRY_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define kStarboardExtensionPleasantryName "dev.starboard.extension.Pleasantry"

typedef struct StarboardExtensionPleasantryApi {
  // Name should be the string |kStarboardExtensionPleasantryName|.
  // This helps to validate that the extension API is correct.
  const char* name;

  // This specifies the version of the API that is implemented.
  uint32_t version;

  // The fields below this point were added in version 1 or later.
  const char* greeting;

  // The fields below this point were added in version 2 or later.
  const char* (*GetFarewell)();

} StarboardExtensionPleasantryApi;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // STARBOARD_EXTENSION_PLEASANTRY_H_
```

</details>

<details>
    <summary style="display:list-item">Updated contents of
    `starboard/linux/shared/pleasantry.cc`.</summary>

```
#include "starboard/linux/shared/pleasantry.h"

#include <stdlib.h>

#include "starboard/common/time.h"
#include "starboard/extension/pleasantry.h"
#include "starboard/system.h"

namespace starboard {
namespace shared {

namespace {

const char* kGreeting = "Happy debugging!";

const char* kFarewells[] = {
  "Farewell",
  "Take care",
  "Thanks for running Cobalt",
};

const char* GetFarewell() {
  srand (starboard::CurrentPosixTime());
  int pseudo_random_index = rand() % SB_ARRAY_SIZE_INT(kFarewells);
  return kFarewells[pseudo_random_index];
}

const StarboardExtensionPleasantryApi kPleasantryApi = {
  kStarboardExtensionPleasantryName,
  2,
  kGreeting,
  &GetFarewell,
};

}  // namespace

const void* GetPleasantryApi() {
  return &kPleasantryApi;
}

}  // namespace shared
}  // namespace starboard
```

</details>

<details>
    <summary style="display:list-item">`git diff cobalt/browser/main.cc`
    </summary>

```
@@ -19,6 +19,8 @@
 #include "cobalt/browser/application.h"
 #include "cobalt/browser/switches.h"
 #include "cobalt/version.h"
+#include "starboard/extension/pleasantry.h"
+#include "starboard/system.h"

 namespace {

@@ -54,6 +56,14 @@ bool CheckForAndExecuteStartupSwitches() {
   return g_is_startup_switch_set;
 }

+// Get the Pleasantry extension if it's implemented.
+const StarboardExtensionPleasantryApi* GetPleasantryApi() {
+  static const StarboardExtensionPleasantryApi* pleasantry_extension =
+      static_cast<const StarboardExtensionPleasantryApi*>(
+          SbSystemGetExtension(kStarboardExtensionPleasantryName));
+  return pleasantry_extension;
+}
+
 void PreloadApplication(int argc, char** argv, const char* link,
                         const base::Closure& quit_closure,
                         int64_t timestamp) {
@@ -77,6 +87,14 @@ void StartApplication(int argc, char** argv, const char* link,
     return;
   }
   LOG(INFO) << "Starting application.";
+  const StarboardExtensionPleasantryApi* pleasantry_extension =
+      static_cast<const StarboardExtensionPleasantryApi*>(
+          SbSystemGetExtension(kStarboardExtensionPleasantryName));
+  if (pleasantry_extension &&
+      strcmp(pleasantry_extension->name, kStarboardExtensionPleasantryName) == 0 &&
+      pleasantry_extension->version >= 1) {
+    LOG(INFO) << pleasantry_extension->greeting;
+  }
 #if SB_API_VERSION >= 13
   DCHECK(!g_application);
   g_application = new cobalt::browser::Application(quit_closure,
@@ -96,7 +114,14 @@ void StartApplication(int argc, char** argv, const char* link,
 }

 void StopApplication() {
-  LOG(INFO) << "Stopping application.";
+  const StarboardExtensionPleasantryApi* pleasantry_extension = GetPleasantryApi();
+  if (pleasantry_extension &&
+      strcmp(pleasantry_extension->name, kStarboardExtensionPleasantryName) == 0 &&
+      pleasantry_extension->version >= 2) {
+    LOG(INFO) << pleasantry_extension->GetFarewell();
+  } else {
+    LOG(INFO) << "Stopping application.";
+  }
   delete g_application;
   g_application = NULL;
 }
```

</details>

`starboard/linux/shared/pleasantry.h`,
`starboard/linux/shared/BUILD.gn`, and
`starboard/linux/shared/system_get_extensions.cc` should be unchanged from the
Exercise 1 solution.

## Extension testing

Each Starboard Extension has a corresponding test in
`starboard/extension/extension_test.cc` that verifies the extension is
configured correctly for the target platform.

Because some platforms may not implement a particular extension, tests begin
by checking if `SbSystemGetExtension` returns `NULL` for the extension name.
For the `foo` extension, the test begins as follows:

```
TEST(ExtensionTest, Foo) {
  typedef StarboardExtensionFooApi ExtensionApi;
  const char* kExtensionName = kStarboardExtensionFooName;

  const ExtensionApi* extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  if (!extension_api) {
    return;
  }

  // Verifications about the global structure instance, if implemented.
}
```

If `SbSystemGetExtension` does not return `NULL` (indicating the platform
implements the extension), the tests verify that the structure:

*   Has the expected name.
*   Has a version within the valid range for the extension.
*   Contains all members required for the implemented version.
*   Behaves as a singleton.

### Exercise 3: Test your extension

Add a test for your new extension to `starboard/extension/extension_test.cc`.

After writing the test, run it to confirm it passes. The
`starboard/extension/BUILD.gn` file configures the `extension_test` target.
Build this target for your platform and run the resulting executable:

```bash
cobalt/build/gn.py -p linux-x64x11 -c devel --no-rbe
autoninja -C out/linux-x64x11_devel extension_test
out/linux-x64x11_devel/extension_test
```

**Tip:** Since `extension_test` is a GTest target, you can use `--gtest_filter`
to run specific tests. For example, run only your new test using
`--gtest_filter=ExtensionTest.Pleasantry`.

#### Solution to Exercise 3

<details>
    <summary style="display:list-item">Click here to see a solution for the new
    test.</summary>

```
TEST(ExtensionTest, Pleasantry) {
  typedef StarboardExtensionPleasantryApi ExtensionApi;
  const char* kExtensionName = kStarboardExtensionPleasantryName;

  const ExtensionApi* extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  if (!extension_api) {
    return;
  }

  EXPECT_STREQ(extension_api->name, kExtensionName);
  EXPECT_GE(extension_api->version, 1u);
  EXPECT_LE(extension_api->version, 2u);

  EXPECT_NE(extension_api->greeting, nullptr);

  if (extension_api->version >= 2) {
    EXPECT_NE(extension_api->GetFarewell, nullptr);
  }

  const ExtensionApi* second_extension_api =
      static_cast<const ExtensionApi*>(SbSystemGetExtension(kExtensionName));
  EXPECT_EQ(second_extension_api, extension_api)
      << "Extension struct should be a singleton";
}
```

</details>

You'll also want to include the header for the extension, i.e., `#include
"starboard/extension/pleasantry.h"`.

## Contributing a Starboard Extension

Thanks for taking the time to complete the codelab!

**If you want to contribute a Starboard Extension to Cobalt to add functionality
for your platform, start a discussion with the Cobalt team before coding.** File
a feature request [using this template](https://issuetracker.google.com/issues/new?component=181120&template=1820891).

File the feature request with the appropriate priority. The Cobalt team will
review the proposal, and if they approve the use case and design, they will
assign it to you for implementation. Then, follow the
[Contributing to Cobalt](../../contributors/index.md) guide to ensure your code
is compliant for review and submission.
