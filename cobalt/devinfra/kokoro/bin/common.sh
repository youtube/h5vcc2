#!/bin/bash
set -eux

#---------------------------------
# Reusable common kokoro functions.
# --------------------------------


log_cobalt_header () {
  set +x
  echo "############################################################################################################"
  echo "#::'######:::'#######::'########:::::'###::::'##::::'########::::'##::::::::'#######:::'######::::'######::#"
  echo "#:'##... ##:'##.... ##: ##.... ##:::'## ##::: ##::::... ##..::::: ##:::::::'##.... ##:'##... ##::'##... ##:#"
  echo "#: ##:::..:: ##:::: ##: ##:::: ##::'##:. ##:: ##::::::: ##::::::: ##::::::: ##:::: ##: ##:::..::: ##:::..::#"
  echo "#: ##::::::: ##:::: ##: ########::'##:::. ##: ##::::::: ##::::::: ##::::::: ##:::: ##: ##::'####:. ######::#"
  echo "#: ##::::::: ##:::: ##: ##.... ##: #########: ##::::::: ##::::::: ##::::::: ##:::: ##: ##::: ##:::..... ##:#"
  echo "#: ##::: ##: ##:::: ##: ##:::: ##: ##.... ##: ##::::::: ##::::::: ##::::::: ##:::: ##: ##::: ##::'##::: ##:#"
  echo "#:. ######::. #######:: ########:: ##:::: ##: ########: ##::::::: ########:. #######::. ######:::. ######::#"
  echo "#::......::::.......:::........:::..:::::..::........::..::::::::........:::.......::::......:::::......:::#"
  echo "############################################################################################################"
  set -x
}
log_cobalt_header


get_kokoro_env () {
  # Kokoro doesn't provide a direct way to detect which environment the job is
  # running in: https://yaqs.corp.google.com/eng/q/3810666508825133056
  # This is a workaround that uses the artifacts directory where the first path
  # segment is the env: "<environment>/<job name>/<build number>/<date>"
  echo "${KOKORO_BUILD_ARTIFACTS_SUBDIR}" | cut -d'/' -f1
}


configure_environment () {
  # Tell git the repo is safe.
  git config --global --add safe.directory "${WORKSPACE_COBALT}"

  # Use Kokor's default credentials instead of boto file.
  unset BOTO_PATH

  # Add repository root to PYTHONPATH.
  export PYTHONPATH="${WORKSPACE_COBALT}${PYTHONPATH:+:${PYTHONPATH}}"

  # In some instances KOKORO_GOB_BRANCH_src is not set, set by default to 'COBALT'.
  if [ -z "${KOKORO_GOB_BRANCH_src+set}" ]; then
    echo 'WARNING: $KOKORO_GOB_BRANCH_src is unset. Defaulting to "COBALT"'
    export KOKORO_GOB_BRANCH_src="COBALT"
  fi

  # In some instances kokoro uses KOKORO_GOB_BRANCH instead.
  if [ -z "${KOKORO_GOB_BRANCH+set}" ]; then
    echo 'WARNING: $KOKORO_GOB_BRANCH is unset. Defaulting to "COBALT"'
    export KOKORO_GOB_BRANCH="COBALT"
  fi

  env | sort
}

configure_dind_environment () {
  export REGISTRY_PATH="$(get_registry_bucket_path)"

  # Authenticate with GCR or GAR.
  echo "Logging in to registry with access token"
  set +x  #  Temporarily turn off debug logging to hide access token.
  printf "$(get_registry_authentication_token)" | docker login -u \
    oauth2accesstoken --password-stdin "https://${REGISTRY_HOSTNAME}"

  printf "$(get_registry_authentication_token)" | docker login -u \
    oauth2accesstoken --password-stdin "https://marketplace.gcr.io"
  set -x
}

get_registry_authentication_token () {
  # Hide access token in logs.
  set +x
  # Helper method that uses a well-known workaround for authenticating Kokoro
  # workers to access GCP hosted container/artifiact registries.
  # go/gcs-tpc-bigstore-user-guide#authentication-http-bearer-tokensaccess-tokens
  local svc_account="${REGISTRY_METADATA}/instance/service-accounts/default"
  gcloud config set account "$(curl -s -H 'Metadata-Flavor: Google' "${svc_account}/email")"
  local access_token=$(curl -H 'Metadata-Flavor: Google' ${svc_account}/token | cut -d'"' -f 4)
  echo "${access_token}"
  set -x
}

get_registry_bucket_path () {
  # Outputs the fully specified URL to the registry to push/pull images to/from.
  echo "${REGISTRY_HOSTNAME}/${REGISTRY_GCP_PROJECT}/$(get_kokoro_env)"
}

init_gcloud () {
  # Check if gcloud is pre-installed on Kokoro windows container.
  local gcloud_win="c:/gcloud-sdk/google-cloud-sdk/bin/gcloud.cmd"
  if [[ -f "${gcloud_win}" ]]; then
    export GCLOUD="${gcloud_win}"
    return
  fi

  local gcloud="$(command -v gcloud)"

  # Installs Google Cloud CLI if not already present.
  if [[ ! -f "${gcloud}" ]]; then
    apt-get update -y
    apt-get install -y apt-transport-https ca-certificates gnupg
    curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | \
      gpg --dearmor -o /usr/share/keyrings/cloud.google.gpg
    echo "deb [signed-by=/usr/share/keyrings/cloud.google.gpg] https://packages.cloud.google.com/apt cloud-sdk main" | \
      tee -a /etc/apt/sources.list.d/google-cloud-sdk.list
    apt-get update -y
    apt-get install -y google-cloud-cli
    rm -rf "/var/lib/apt/lists"/* "/tmp"/* "/var/tmp"/*
    rm -rf "/var/lib"/{apt,dpkg,cache,log}

    gcloud="$(command -v gcloud)"
  fi

  export GCLOUD="${gcloud}"
}


is_release_build () {
  [[ "${KOKORO_ROOT_JOB_TYPE:-}" == "RELEASE" ]]
}


is_release_config () {
  [[ "${RELEASE_CONFIGS}" =~ "${CONFIG}" ]]
}


run_gn () {
  if [ $# -ne 3 ]; then
    echo "Error: Exactly 3 arguments required!"
    exit 1
  fi

  local out_dir=$1
  local starboard_target_platform=$2
  local gn_arguments=$3

  set +u
  echo 'Running GN...'
  gn gen "${out_dir}" --args="starboard_target_platform=\"${starboard_target_platform}\" \
    ${SB_API_VERSION} \
    ${TARGET_OS} \
    ${TARGET_CPU} \
    ${gn_arguments} \
    build_type=\"${CONFIG}\""
  set -u
  gn check "${out_dir}"
}


ninja_build () {
  if [ $# -ne 2 ]; then
    echo "Error: Exactly 2 arguments required!"
    exit 1
  fi

  local out_dir=$1
  local targets=("$2")

  echo 'Running Ninja build...'
  time ninja -C "${out_dir}" ${targets[@]}

  echo "Generated build info"
  build_info_file="${out_dir}/gen/build_info.json"
  if [ -f $build_info_file ]; then
    echo "$(<$build_info_file)"
  else
    echo "No build_info.json file was generated."
  fi
}

run_package_release_pipeline () {
  # NOTE: For DinD builds, we only run the GN and Ninja steps in the container.
  # Artifacts and build-products from these steps are used in subsequent steps
  # for packaging and nightly post-build tasks, and do not need to be run in
  # the inner container environment (which has build tools and deps).

  if is_release_build && is_release_config; then
    local out_dir="${WORKSPACE_COBALT}/out/${TARGET_PLATFORM}_${CONFIG}"
    local package_dir="${WORKSPACE_COBALT}/package/${PLATFORM}_${CONFIG}"
    mkdir -p "${package_dir}"

    # Create release package
    export PYTHONPATH="${WORKSPACE_COBALT}"

    # IMPORTANT: chromedriver must be built without starboardizations. We ensure
    # that the biary is built with the linux-x64x11-no-starboard config in a
    # previous build step. Then copy the file into this out directory to
    # simulate having built it in-situ (even though that's not possible). This
    # simplifies the execution of the packaging scripts.
    if [[ "${TARGET_PLATFORM}" =~ "linux" ]]; then
      local src_platform="linux-x64x11-no-starboard"
      local src_out="${WORKSPACE_COBALT}/out/${src_platform}_${CONFIG}"
      local dst_out="${WORKSPACE_COBALT}/out/${TARGET_PLATFORM}_${CONFIG}"
      cp "${src_out}/chromedriver" "${dst_out}/chromedriver"
    fi

    # Use config-specific (e.g. package_devel.json) if it exists, otherwise fallback to package.json.
    local json_path="${WORKSPACE_COBALT}/cobalt/build/${PACKAGE_PLATFORM}/package.json"
    local config_json_path="${WORKSPACE_COBALT}/cobalt/build/${PACKAGE_PLATFORM}/package_${CONFIG}.json"
    if [[ -f "${config_json_path}" ]]; then
      json_path="${config_json_path}"
    fi

    # NOTE: Name is required because the Json recipe is not platform and config
    # specific. GCS upload is also done separately because the Json recipe is
    # not branch, date, and build number specific though this can be added.
    python3 "${WORKSPACE_COBALT}/cobalt/build/packager.py" \
      --print_contents \
      --name="${PLATFORM}_${CONFIG}" \
      --json_path="${json_path}" \
      --out_dir="${out_dir}" \
      --package_dir="${package_dir}"

    # Upload release package
    local bucket="cobalt-internal-build-artifacts"
    if [[ "$(get_kokoro_env)" == "qa" ]]; then
      bucket="cobalt-internal-build-artifacts-qa"
    fi
    local gcs_archive_path="gs://${bucket}/${PLATFORM}${GCS_PLATFORM_SUFFIX:-}_${KOKORO_GOB_BRANCH_src}/$(date +%F)/${KOKORO_ROOT_BUILD_NUMBER}/"
    init_gcloud
    # Ensure that only package directory contents are uploaded and not the
    # directory itself.
    "${GCLOUD}" storage cp --recursive "${package_dir}/." "${gcs_archive_path}"
  fi
}
