#!/bin/bash
set -ueEx

# Load common routines.
. $(dirname "$0")/common.sh

# Using repository root as work directory.
if [[ -d "${KOKORO_ARTIFACTS_DIR}/github" ]]; then
  export GCLIENT_ROOT="${KOKORO_ARTIFACTS_DIR}/github"
else
  export GCLIENT_ROOT="${KOKORO_ARTIFACTS_DIR}/git"
fi
export WORKSPACE_COBALT="${GCLIENT_ROOT}/src"
cd "${WORKSPACE_COBALT}"

# Clean up workspace on exit or error.
trap "bash ${WORKSPACE_COBALT}/cobalt/devinfra/kokoro/bin/cleanup.sh" EXIT INT TERM

# TODO(b/538697105): Unify build_mac.sh and presubmit_build_mac.sh, and consolidate
# tvOS app code-signing logic into the unified CI build script.
resign_tvos_app_if_needed () {
  local target_name="$1"
  local out_dir="$2"

  if [[ -z "${KOKORO_PIPER_DIR:-}" ]]; then
    echo "Not running in internal Kokoro (KOKORO_PIPER_DIR absent). Skipping re-signing for simulator build."
    return 0
  fi

  local tvos_profile="${KOKORO_PIPER_DIR}/google3/googlemac/iPhone/Shared/ProvisioningProfiles/YouTube/YouTube_Dev_tvOS.mobileprovision"
  if [[ ! -f "${tvos_profile}" ]]; then
    echo "ERROR: Provisioning profile not found at ${tvos_profile}" >&2
    exit 1
  fi

  echo "Embedding ${tvos_profile} into ${target_name}.app..."
  cp -f "${tvos_profile}" "${out_dir}/${target_name}.app/embedded.mobileprovision"

  # Extract entitlements from the provisioning profile
  local entitlements_plist="${out_dir}/${target_name}_entitlements.plist"
  if ! security cms -D -i "${tvos_profile}" 2>/dev/null | plutil -extract Entitlements xml1 -o "${entitlements_plist}" - 2>/dev/null; then
    echo "ERROR: Failed to extract entitlements from ${tvos_profile}" >&2
    exit 1
  fi

  # Re-sign app bundle using Kokoro's installed development certificate
  echo "Signing ${target_name}.app for physical lab devices..."
  codesign --force --deep --sign "Apple Development" --entitlements "${entitlements_plist}" "${out_dir}/${target_name}.app"
  rm -f "${entitlements_plist}"
}

# Mac build script.
pipeline () {
  # Run common configuration steps.
  configure_environment

  # Run mac specific setup steps.
  setup_mac

  git config --global --add safe.directory "${GCLIENT_ROOT}/src"
  local git_url="$(git -C "${GCLIENT_ROOT}/src" remote get-url origin)"

  # Set up gclient and run sync.
  ##############################################################################
  cd "${GCLIENT_ROOT}"
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git tools/depot_tools --filter=blob:none
  export PATH="${PATH}:${GCLIENT_ROOT}/tools/depot_tools"
  # Conditionally enable RBE variables
  local custom_vars=""
  if [[ "${CONFIG}" == "devel" || "${CONFIG}" == "qa" ]]; then
    custom_vars="\"custom_vars\": {'download_remoteexec_cfg': True, 'rbe_instance': 'projects/cobalt-actions-prod/instances/default_instance', 'reapi_backend_config_path': 'cobalt.star'},"
  fi

  # Write the .gclient file directly with managed = False
  cat <<EOF > .gclient
solutions = [
  { "name"        : 'src',
    "url"         : '${git_url}',
    "deps_file"   : 'DEPS',
    "managed"     : False,
    "custom_deps" : {
    },
    ${custom_vars}
  },
]
target_os=['ios']
EOF
  # -D, --delete_unversioned_trees
  # -f, --force force update even for unchanged modules
  # -R, --reset resets any local changes before updating (git only)
  luci-auth context -service-account-json "${KOKORO_KEYSTORE_DIR}/${GCLOUD_KEY_FILE_NAME}" -- \
  gclient sync -v \
    --shallow \
    --no-history \
    -D \
    -f \
    -R
  build_telemetry opt-out

  # Run GN and Ninja.
  ##############################################################################
  cd "${GCLIENT_ROOT}/src"
  local rbe_flag="--no-rbe"
  if [[ "${CONFIG}" == "devel" ]] || [[ "${CONFIG}" == "qa" ]]; then
    rbe_flag=""
  fi

  cobalt/build/gn.py -p "${TARGET_PLATFORM}" -C "${CONFIG}" \
    --script-executable=/usr/bin/python3 ${rbe_flag}
  for gn_arg in ${EXTRA_GN_ARGUMENTS:-}; do
    echo "${gn_arg}" >> "out/${TARGET_PLATFORM}_${CONFIG}/args.gn"
  done
  # Build Cobalt.
  local out_dir="${WORKSPACE_COBALT}/out/${TARGET_PLATFORM}_${CONFIG}"

  # Extract test targets from JSON for non-release configs (e.g. devel).
  local json_targets=""
  if ! is_release_config; then
    local test_targets_json="${WORKSPACE_COBALT}/cobalt/build/testing/targets/${TARGET_PLATFORM}/test_targets.json"
    if [[ -f "${test_targets_json}" ]]; then
      # Extract test targets from the JSON file (list of dicts schema)
      # and format them as a space-separated list of target names (after the colon).
      json_targets=$(python3 -c "import json, re; data=json.loads(re.sub(r'//.*', '', open('${test_targets_json}').read())); targets=[e['target'] for e in data if isinstance(e, dict) and 'target' in e]; print(' '.join(t.split(':')[-1] for t in targets))")
      GN_TARGET="${GN_TARGET:-} ${json_targets}"
    fi
  fi

  autoninja -C "${out_dir}" ${GN_TARGET}

  # Package, archive, and upload to GCS.
  ##############################################################################
  if [[ -z "${KOKORO_BUILD_ID:-}" ]]; then
    echo "ERROR: KOKORO_BUILD_ID environment variable is required."
    exit 1
  fi
  local build_id="${KOKORO_BUILD_ID}"
  local bucket="${COBALT_GCS_BUCKET:-cobalt-unittest-storage}"
  local gcs_archive_path="gs://${bucket}/kokoro/build/${TARGET_PLATFORM}_${CONFIG}/${build_id}/"

  for target in ${GN_TARGET}; do
    # Extract target name (e.g. base:base_unittests -> base_unittests)
    local target_name="${target##*:}"

    if [[ "${target_name}" == "cobalt_archive" ]]; then
      echo "Packaging and archiving tvOS library build (cobalt_archive)..."
      local package_dir="${WORKSPACE_COBALT}/package/${TARGET_PLATFORM}_${CONFIG}"
      mkdir -p "${package_dir}"
      local build_info_path="${out_dir}/gen/build_info.json"

      # Create release package.
      python3 "${WORKSPACE_COBALT}/cobalt/devinfra/kokoro/build/tvos/tvos_packager.py" \
        "${out_dir}" \
        "${package_dir}" \
        "${build_info_path}"

      # Create tar.gz archive.
      local archive_file="${package_dir}.tar.gz"
      echo "Creating archive ${archive_file}..."
      tar -czf "${archive_file}" -C "${WORKSPACE_COBALT}/package" "${TARGET_PLATFORM}_${CONFIG}"

      # Upload to GCS.
      echo "Uploading archive to ${gcs_archive_path}..."
      "${GSUTIL}" cp "${archive_file}" "${gcs_archive_path}"

    elif [[ " ${json_targets} " == *" ${target_name} "* ]] && [[ -d "${out_dir}/${target_name}.app" ]]; then
      echo "Archiving and uploading test package ${target_name}.app..."

      # Re-sign app for physical lab devices if running in internal Kokoro
      resign_tvos_app_if_needed "${target_name}" "${out_dir}"

      local archive_file="${WORKSPACE_COBALT}/package/${target_name}.tar.gz"
      mkdir -p "${WORKSPACE_COBALT}/package"

      # Create tar.gz archive including iossim if available.
      if [[ -f "${out_dir}/iossim" ]]; then
        echo "Including iossim in the archive..."
        tar -czf "${archive_file}" -C "${out_dir}" "${target_name}.app" "iossim"
      else
        tar -czf "${archive_file}" -C "${out_dir}" "${target_name}.app"
      fi

      # Upload to GCS.
      echo "Uploading archive ${target_name}.tar.gz to ${gcs_archive_path}..."
      "${GSUTIL}" cp "${archive_file}" "${gcs_archive_path}"
    fi
  done
}


setup_mac () {
  # Pull signing profiles.
  mkdir -p "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
  if [ -n "${KOKORO_PIPER_DIR:-}" ]; then
    cp -f ${KOKORO_PIPER_DIR}/google3/googlemac/iPhone/Shared/ProvisioningProfiles/*.mobileprovision \
        "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
  else
    echo "WARNING: KOKORO_PIPER_DIR is unset. Skipping provisioning profiles copy."
  fi

  export DEVELOPER_DIR="/Applications/Xcode_${XCODE_VERSION}.app/Contents/Developer"

  # Configure PATH.
  export PATH="/usr/local/opt/bison/bin:/usr/libexec:$PATH"
  # "gcloud storage" is GA and recommened over "gsutil". See
  # https://cloud.google.com/storage/docs/gsutil#gcloud-storage-command for
  # details.
  gcloud_storage_shim() {
    gcloud storage "$@"
  }
  export GSUTIL="gcloud_storage_shim"

  # Authenticate gcloud service account if key is available in keystore.
  if [ -n "${KOKORO_KEYSTORE_DIR:-}" ] && [ -n "${GCLOUD_KEY_FILE_NAME:-}" ] && [ -f "${KOKORO_KEYSTORE_DIR}/${GCLOUD_KEY_FILE_NAME}" ]; then
    echo "Authenticating Gcloud Service Account..."
    gcloud auth activate-service-account --key-file="${KOKORO_KEYSTORE_DIR}/${GCLOUD_KEY_FILE_NAME}"
    export SISO_CREDENTIAL_HELPER="gcloud"
  fi
}

# Run the pipeline.
pipeline
