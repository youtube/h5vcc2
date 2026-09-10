# Copyright 2021 The Cobalt Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ARG BASE_OS
ARG BASE_OS_TAG
FROM ${BASE_OS:-marketplace.gcr.io/google/debian11}:${BASE_OS_TAG:-latest}

COPY base/clean-after-apt.sh /opt/clean-after-apt.sh

ENV PYTHONUNBUFFERED 1

# === Ignore expired InRelease repository metadata (e.g. Debian Bullseye security archive)
RUN echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf.d/99no-check-valid-until

# === Install common dependencies
RUN apt update -qqy -o Acquire::Check-Valid-Until=false \
    && apt -qqy upgrade -o Acquire::Check-Valid-Until=false \
    && apt install -qqy --no-install-recommends \
        python3-dev \
        python3-pip \
        python3-requests \
        python3-setuptools \
        python3-six \
        git ccache \
        curl xz-utils \
    && /opt/clean-after-apt.sh

CMD ["/usr/bin/python3","--version"]
