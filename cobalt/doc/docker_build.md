# Cobalt Docker builds

Cobalt includes [Docker image][docker.com](https://www.docker.com/)
definitions for simplifying managing build environments.

The instructions below assume Docker is installed and is able to run basic
[`hello-world` verification](https://docs.docker.com/get-started/#test-docker-installation).
`docker compose` command is expected to be available as well.

## Usage

The simplest usage is:

  `docker compose run <platform>`

By default, a debug build will be built, with `cobalt` as a target.
You can override this with an environment variable, e.g.

  `docker compose run -e CONFIG=devel -e TARGET=nplb <platform>`

where config is one of the four optimization levels, debug, devel, qa and gold,
and target is the build target passed to `ninja`

See [Cobalt README](../../README.md#build-types)
for full details.

Builds will be available in your `${COBALT_SRC}/out` directory.

NB! Note that Docker runs processes as root user by default, hence output
files in `src/out/<platform>` directory have `root` as file owner.

## Customization

To parametrize base operating system images used for the build, pass BASE_OS
as an argument to docker compose as follows:

  `docker compose build --build-arg BASE_OS="ubuntu:bionic" base`

This parameter is defined in `docker/linux/base/Dockerfile` and is passed to
Docker `FROM ...` statement.

Available parameters for customizing container execution are:

 - **BASE_OS**: passed to `base` image at build time to select a Debian-based
    base os image and version. Defaults to Debian 9. `ubuntu:bionic` and
    `ubuntu:xenial` are other tested examples.
 - **PLATFORM**: Cobalt build platform, passed to GN
 - **CONFIG**: Cobalt build config, passed to GN. Defaults to `debug`
 - **TARGET**: Build target, passed to `ninja`

The `docker-compose.yml` contains the currently defined experimental build
configurations. Edit or add new `service` entries as needed, to build custom
configurations.

## Pre-built images

Pre-built images are available at https://github.com/orgs/youtube/packages?repo_name=cobalt

For example, a container for building Android platform from main branch can be pulled as follows:

```
docker pull ghcr.io/youtube/cobalt/cobalt-build-android:main
```

Similarly, from LTS branch for Evergreen platform:

```
docker pull ghcr.io/youtube/cobalt/cobalt-build-evergreen:23.lts
```

## Troubleshooting

To debug build issues, enter the shell of the corresponding build container by
launching the bash shell, i.e.

  `docker compose run linux-x64x11 /bin/bash`

and try to build cobalt [with the usual `gn / ninja` flow](../../README.md#building-and-running-the-code).
