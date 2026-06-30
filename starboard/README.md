# Starboard

Starboard is Cobalt's porting layer and OS abstraction. It attempts to encompass
all the platform-specific functionality that Cobalt actually uses, and nothing
that it does not.


## GN Migration Notice

Cobalt and Starboard have been migrated from the GYP build system to the GN
build system. This readme only contains instructions for GN, as GYP is no
longer supported.


## Documentation

See [`starboard/doc`](doc) for more detailed documentation.


## Interesting Source Locations

All source locations are specified relative to `starboard/` (this directory).

  * [`.`](.) - This is the root directory for the Starboard project, and
    contains all the public headers that Starboard defines.
  * [`examples/`](examples) - Example code demonstrating various aspects of
    Starboard API usage.
  * [`stub/`](stub) - The home of the Stub Starboard implementation. This
    contains a `BUILD.gn` file that defines a library with all the
    source files needed to provide a complete linkable Starboard implementation.
  * [`nplb/`](nplb) - "No Platform Left Behind," Starboard's platform
    verification test suite.
  * [`shared/`](shared) - The home of all code that can be shared between
    Starboard implementations. Subdirectories delimit code that can be shared
    between platforms that share some facet of their OS API.


## Quick Guide to Starting a Port

### I. Enumerate and Name Your Platform Configurations

Before starting a Cobalt/Starboard port, first you will need to define the
canonical names for your set of platform configurations. These will be used when
organizing the code for your platforms.

What determines what goes into one platform configuration versus another? A
platform configuration has a one-to-one mapping to a production binary. So, if
you will need to produce a new binary, you are going to need a new platform
configuration for that.

The recommended naming convention for a `<platform-configuration>` is:

    <family-name>-<binary-variant>

Where `<family-name>` is a name specific to the family of products you are
porting to Starboard and `<binary-variant>` is one or more tokens that uniquely
describe the specifics of the binary you want that configuration to produce.

For example, let's say your company is named BobCo. BobCo employs multiple
different device architectures so it will need to define multiple platform
configurations.

All the BobCo devices are called BobBox, so it's a reasonable choice as a
product `<family-name>`. But they have both big- and little-endian ARM
chips. So they might define two platform configurations:

  1. `bobbox-armeb` - For big-endian ARM devices.
  1. `bobbox-armel` - For little-endian ARM devices.


### II. Choose a Location in the Source Tree for Your Starboard Port

To be perfectly compatible with the Cobalt source tree layout, any code that is
written by a party that isn't the Cobalt team should be in the
`third_party/` directory. The choice is up to you, but we recommend that you
follow this practice, even if, as we expect to be common, you do not plan on
sharing your Starboard implementation with anyone.

Primarily, following this convention ensures that no future changes to Cobalt or
Starboard will conflict with your source code additions. Starboard is intended
to be a junction where new Cobalt versions or Starboard implementations can be
replaced without significant (and hopefully, any) code changes.

We recommend that you place your code here in the source tree:

    third_party/starboard/<family-name>/

With subdirectories:

  * `shared/` - For code shared between architectures within a product family.
  * `<binary-variant>/` - For any code that is specific to a specific binary
    variant. Each one of these must at least have `BUILD.gn`,
    `configuration_public.h`,
    `platform_configuration/BUILD.gn`,
    `platform_configuration/configuration.gni`, and `toolchain/BUILD.gn` files.

In the BobCo's BobBox example, we would see something like:

  * `third_party/starboard/bobbox/`
      * `shared/`
      * `armeb/`
          * `platform_configuration/`
            * `BUILD.gn`
            * `configuration.gni`
          * `toolchain/`
            * `BUILD.gn`
          * `BUILD.gn`
          * `configuration_public.h`
      * `armel/`
          * `platform_configuration/`
            * `BUILD.gn`
            * `configuration.gni`
          * `toolchain/`
            * `BUILD.gn`
          * `BUILD.gn`
          * `configuration_public.h`

And so on.


### III. Base Your Port on a Reference Port

You can start off by copying files from a reference port to your port's
location. Currently these reference ports include:

  * `starboard/stub`
  * `starboard/linux`

The platform's `BUILD.gn` contains absolute paths, so the paths will still be
valid if you copy it to a new directory. You can then incrementally replace
files with new implementations as necessary.

The cleanest, simplest starting point is from the Stub reference
implementation. Nothing will work, but you should be able to compile and link it
with your toolchain. You can then replace stub implementations with
implementations from `starboard/shared` or your own custom implementations
module-by-module, until you have gone through all modules.

You may also choose to copy the Desktop Linux port and work backwards fixing
things that don't compile or work on your platform.

For example, for `bobbox-armel`, you might do:

    mkdir -p third_party/starboard/bobbox
    cp -R starboard/stub third_party/starboard/bobbox/armel

Modify the files in `<binary-variant>/` as appropriate (you will probably be
coming back to these files a lot).

Update `<binary-variant>/BUILD.gn` to point at all the source files that you
want to build as your new Starboard implementation. The `//` expression in GN
refers to the top-level directory of your source tree. Otherwise, files are
assumed to be relative to the directory the `BUILD.gn` or `.gni` file is in.
The `BUILD.gn` file contains absolute paths, so the paths will still be valid
if you copy it to a new directory. You can then incrementally replace files
with new implementations as necessary.

In order to use a new platform configuration in a build, you need to ensure that
you have a `BUILD.gn`, `toolchain/BUILD.gn`,
`platform_configuration/configuration.gni`, and
`platform_configuration/BUILD.gn` in their own directory for each binary
variant, plus the header files `configuration_public.h`.
You must add your platform name to `starboard/build/platforms.py` along with
the path to the port to be able to build it.


### IV. A New Port, Step-by-Step

  1. Recursively copy `starboard/stub` to
     `third_party/starboard/<family-name>/<binary-variant>`.  You may also
     consider copying from another reference platform, like `linux-x64x11`.
  1. Add your platform and path to `starboard/build/platforms.py`.
  1. In `platform_configuration/configuration.gni`
      1. Delete variables in the file that are not needed for your platform.
      1. Set `gl_type` to the appropriate value if it is not the default
         `system_gles2`.
      1. Set `enable_in_app_dial` to `true` or `false`. This enables or
         disables the DIAL server that runs inside Cobalt, only when Cobalt is
         running. You do not want in-app DIAL if you already have system-wide
         DIAL support.
  1. In `platform_configuration/BUILD.gn`
      1. Update your toolchain command-line flags and libraries. Make sure you
         don't assume a particular workstation layout, as it is likely to be
         different for someone else.
  1. In `toolchain/BUILD.gn`
      1. Either use the `clang_toolchain` template and pass the base path to
         your toolchain, or use the `gcc_toolchain` and pass the full path to
         each tool you use.
      1. Set `is_clang = true` in `toolchain_args` in `gcc_toolchain` if the
         toolchain uses clang.
  1. Go through `configuration_public.h` and adjust all the configuration values
     as appropriate for your platform.
  1. Update `BUILD.gn` to point at all the source files you want to build as
     part of your new Starboard implementation (as mentioned above).

If you want to use `cobalt/build/gn.py`, you'll also need a
`third_party/starboard/<family-name>/<binary-variant>/args.gn` file. This
should contain the gn arguments necessary to build your platform. Pay
particular attention to `target_platform`, `target_os`, `target_cpu`, and
`is_clang`. The defaults for each can be found in
`starboard/build/config/BUILDCONFIG.gn`. If you don't care about using `gn.py`,
all of these arguments can be passed using `gn args` or `gn gen` with `--args`.
For example, the first command below might instead look like
`gn gen out/bobbox-armeb_debug --args='target_platform="bobbox-armeb" build_type="debug"'`.

You should now be able to run GN with your new port. From your the top level
directory:

    $ python cobalt/build/gn.py -c debug -p bobbox-armeb
    $ ninja -C out/bobbox-armeb_debug nplb

This will attempt to build the "No Platform Left Behind" test suite with your
new Starboard implementation, and you are ready to start porting!

## Suggested Implementation Order

When bringing up a new Starboard platform, it is suggested that you try to get
the NPLB tests passing module-by-module. Because of dependencies between
modules, you will find it easier to get some modules passing sooner than other
modules.

Here's a recommended module implementation order in which to get things going
(still significantly subject to change based on feedback):

  1. Configuration
  1. main(), Application, & Event Pump (i.e. the call into SbEventHandle)
  1. Memory
  1. Byte Swap
  1. Time
  1. String/Character/Double
  1. Log
  1. File
  1. Directory
  1. System
  1. Atomic
  1. Thread & Thread Types
  1. Mutex
  1. Condition Variable
  1. Once
  1. Socket
  1. SocketWaiter
  1. Window
  1. Input
  1. Blitter (if applicable)
  1. Audio Sink
  1. Media & Player
  1. DRM
  1. TimeZone
  1. User
  1. Storage
