ARG BASE_IMAGE_SHA=sha256:be15f84c44c4ee68b4f187128f0278df1b0f424c04fb5f08b098967d5b896388

FROM marketplace.gcr.io/google/debian12@${BASE_IMAGE_SHA}

# Install any necessary dependencies
RUN apt-get update && apt-get install -y \
    curl \
    dbus-x11 \
    ffmpeg \
    git \
    jq \
    libasound2 \
    libatomic1 \
    libdbus-1-3 \
    libegl1 \
    libegl1-mesa \
    libegl-mesa0 \
    libgbm-dev \
    libglapi-mesa \
    libgles2-mesa \
    libglib2.0-0 \
    libnss3 \
    libxcomposite-dev \
    openbox \
    python3 \
    python3-dev \
    x11-xserver-utils \
    xcompmgr \
    xcvt \
    xserver-xorg-core \
    xserver-xorg-video-dummy \
    xvfb \
    xz-utils \
    zstd \
    && rm -rf /var/lib/apt/lists/*

ARG GCLOUD=google-cloud-cli-linux-x86_64.tar.gz
RUN cd /tmp \
    && curl -L -O "https://dl.google.com/dl/cloudsdk/channels/rapid/downloads/${GCLOUD}" \
    && tar xf "${GCLOUD}" -C /usr/local --strip-components=1 google-cloud-sdk/bin google-cloud-sdk/lib \
    && rm "${GCLOUD}" \
    && gcloud --version

# This locale satisfies SbSystemGetLocaleIdTest.
ENV LANG=en_US.UTF-8

# Satisfies SbGlesTest.SupportsOpenGles32OrHigher when running ANGLE on software GL in Linux containers.
ENV ANGLE_GL_VERSION="OpenGL ES 3.2.0 (ANGLE 2.1 git hash: unknown hash)"
