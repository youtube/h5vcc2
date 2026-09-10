// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/op_install.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/crx_file/crx_verifier.h"
#include "components/update_client/configurator.h"
#include "components/update_client/crx_cache.h"
#include "components/update_client/pipeline_util.h"
#include "components/update_client/protocol_definition.h"
#include "components/update_client/task_traits.h"
#include "components/update_client/unpacker.h"
#include "components/update_client/unzipper.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_client_errors.h"
#include "third_party/puffin/src/include/puffin/puffpatch.h"

#if BUILDFLAG(IS_STARBOARD)
#include "components/update_client/cobalt_slot_management.h"
#include "components/update_client/utils.h"
#include "starboard/extension/installation_manager.h"
#endif

namespace update_client {

namespace {

// The sequence of calls is:
//
// [Original Sequence]      [Blocking Pool]
//
// CrxCache::Put (optional)
// Unpack
// Unpacker::Unpack
// Install
//                          InstallBlocking
//                          installer->Install
// CallbackChecker::Done
//                          [lambda to delete unpack path]
// InstallComplete
// [original callback]

// CallbackChecker ensures that a progress callback is not posted after a
// completion callback. It is only accessed and modified on the main sequence.
// Both callbacks maintain a reference to an instance of this class.
class CallbackChecker : public base::RefCountedThreadSafe<CallbackChecker> {
 public:
  CallbackChecker(
      base::OnceCallback<void(const CrxInstaller::Result&)> callback,
      CrxInstaller::ProgressCallback progress_callback)
      : callback_(std::move(callback)), progress_callback_(progress_callback) {}
  CallbackChecker(const CallbackChecker&) = delete;
  CallbackChecker& operator=(const CallbackChecker&) = delete;

  void Progress(int progress) { progress_callback_.Run(progress); }

  void Done(const CrxInstaller::Result& result) {
    progress_callback_ = base::DoNothing();
    std::move(callback_).Run(result);
  }

 private:
  friend class base::RefCountedThreadSafe<CallbackChecker>;
  ~CallbackChecker() = default;
  base::OnceCallback<void(const CrxInstaller::Result&)> callback_;
  CrxInstaller::ProgressCallback progress_callback_;
};

// Runs on the original sequence.
void InstallComplete(
    base::OnceCallback<void(const CrxInstaller::Result&)>
        installer_result_callback,
#if BUILDFLAG(IS_STARBOARD)
    base::OnceCallback<void(base::expected<OperationResult, CategorizedError>)>
#else
    base::OnceCallback<void(base::expected<base::FilePath, CategorizedError>)>
#endif
        callback,
    base::RepeatingCallback<void(base::Value::Dict)> event_adder,
#if BUILDFLAG(IS_STARBOARD)
    const OperationResult& crx_operation_result,
#else
    base::FilePath crx_file,
#endif
    const CrxInstaller::Result& result) {
  event_adder.Run(
      MakeSimpleOperationEvent(result.result, protocol_request::kEventCrx3));
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(installer_result_callback), result));
  if (result.result.category != ErrorCategory::kNone) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), base::unexpected(result.result)));
    return;
  }
#if BUILDFLAG(IS_STARBOARD)
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), crx_operation_result));
#else
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), crx_file));
#endif
}

#if !BUILDFLAG(IS_STARBOARD)
// Runs in the blocking thread pool.
void InstallBlocking(
    CrxInstaller::ProgressCallback progress_callback,
    base::OnceCallback<void(const CrxInstaller::Result&)> callback,
    const base::FilePath& unpack_path,
    const std::string& public_key,
    std::unique_ptr<CrxInstaller::InstallParams> install_params,
    scoped_refptr<CrxInstaller> installer) {
  installer->Install(unpack_path, public_key, std::move(install_params),
                     progress_callback, std::move(callback));
}
#endif  // !BUILDFLAG(IS_STARBOARD)

// Runs on the original sequence.
void Install(base::OnceCallback<void(const CrxInstaller::Result&)> callback,
             std::unique_ptr<CrxInstaller::InstallParams> install_params,
             scoped_refptr<CrxInstaller> installer,
             CrxInstaller::ProgressCallback progress_callback,
#if BUILDFLAG(IS_STARBOARD)
             PersistedData* metadata,
             const std::string& id,
             const OperationResult& crx_operation_result,
#endif
             const Unpacker::Result& result) {
  if (result.error != UnpackerError::kNone) {
#if BUILDFLAG(IS_STARBOARD)
    // When there is an error unpacking the downloaded CRX, such as a failure to
    // verify the package, we clear out any drain files.
#if defined(IN_MEMORY_UPDATES)
    if (base::DirectoryExists(crx_operation_result.installation_dir)) {
#else  // defined(IN_MEMORY_UPDATES)
    if (base::DirectoryExists(crx_operation_result.response.DirName())) {
#endif  // defined(IN_MEMORY_UPDATES)
      const auto* installation_api =
          static_cast<const CobaltExtensionInstallationManagerApi*>(
              SbSystemGetExtension(kCobaltExtensionInstallationManagerName));
      if (installation_api) {
        CobaltSlotManagement cobalt_slot_management;
        if (cobalt_slot_management.Init(installation_api)) {
          cobalt_slot_management.CleanupAllDrainFiles();
        }
      }
    }
#endif  // #if BUILDFLAG(IS_STARBOARD)
    std::move(callback).Run(
        CrxInstaller::Result({.category = ErrorCategory::kUnpack,
                              .code = static_cast<int>(result.error),
                              .extra = result.extended_error}));
    return;
  }

  progress_callback.Run(-1);

#if BUILDFLAG(IS_STARBOARD)
  InstallError install_error = InstallError::NONE;
  const CobaltExtensionInstallationManagerApi* installation_api =
      static_cast<const CobaltExtensionInstallationManagerApi*>(
          SbSystemGetExtension(kCobaltExtensionInstallationManagerName));
  if (!installation_api) {
    LOG(ERROR) << "Failed to get installation manager api.";
    install_error = InstallError::GENERIC_ERROR;
  } else if (crx_operation_result.installation_index == IM_EXT_INVALID_INDEX) {
    LOG(ERROR) << "Installation index is invalid.";
    install_error = InstallError::GENERIC_ERROR;
  } else {
    char app_key[IM_EXT_MAX_APP_KEY_LENGTH];
    base::Version manifest_version = ReadEvergreenVersion(result.unpack_path);

    if (installation_api->GetAppKey(app_key, IM_EXT_MAX_APP_KEY_LENGTH) ==
        IM_EXT_ERROR) {
      LOG(ERROR) << "Failed to get app key.";
      install_error = InstallError::GENERIC_ERROR;
    } else if (!manifest_version.IsValid()) {
      LOG(ERROR) << "Invalid Evergreen manifest. Installation aborted.";
      install_error = InstallError::GENERIC_ERROR;
    } else if (CobaltFinishInstallation(
                   installation_api, crx_operation_result.installation_index,
                   result.unpack_path.value(), app_key)) {
      // Write the version of the unpacked update package to the persisted data.
      if (metadata != nullptr) {
        metadata->SetLastInstalledEgAndSbVersion(
            id, manifest_version.GetString(), std::to_string(SB_API_VERSION));
      }
    } else {
      LOG(ERROR) << "CobaltFinishInstallation failed.";
      install_error = InstallError::GENERIC_ERROR;
    }
  }
  std::move(callback).Run(CrxInstaller::Result(install_error));
#else
  // Prepare the callbacks. Delete unpack_path when the completion
  // callback is called.
  auto checker = base::MakeRefCounted<CallbackChecker>(
      base::BindOnce(
          [](base::OnceCallback<void(const CrxInstaller::Result&)> callback,
             const base::FilePath& unpack_path,
             const CrxInstaller::Result& result) {
            base::ThreadPool::PostTaskAndReply(
                FROM_HERE, kTaskTraits,
                base::BindOnce(IgnoreResult(&base::DeletePathRecursively),
                               unpack_path),
                base::BindOnce(std::move(callback), result));
          },
          std::move(callback), result.unpack_path),
      progress_callback);

  // Run installer.
  base::ThreadPool::PostTask(
      FROM_HERE, kTaskTraits,
      base::BindOnce(&InstallBlocking,
                     base::BindPostTaskToCurrentDefault(base::BindRepeating(
                         &CallbackChecker::Progress, checker)),
                     base::BindPostTaskToCurrentDefault(
                         base::BindOnce(&CallbackChecker::Done, checker)),
                     result.unpack_path, result.public_key,
                     std::move(install_params), installer));
#endif
}

// Runs on the original sequence.
void Unpack(base::OnceCallback<void(const Unpacker::Result&)> callback,
#if BUILDFLAG(IS_STARBOARD)
            const OperationResult& crx_operation_result,
#else
            const base::FilePath& crx_file,
#endif
            std::unique_ptr<Unzipper> unzipper,
            const std::vector<uint8_t>& pk_hash,
            crx_file::VerifierFormat crx_format,
            base::expected<base::FilePath, UnpackerError> cache_result) {
  if (!cache_result.has_value()) {
    // Caching is optional: continue with the install, but add a task to clean
    // up crx_file.
#if BUILDFLAG(IS_STARBOARD)
#if !defined(IN_MEMORY_UPDATES)
    callback = base::BindOnce(
        [](const OperationResult& crx_operation_result,
           base::OnceCallback<void(const Unpacker::Result&)> callback,
           const Unpacker::Result& result) {
          base::ThreadPool::PostTaskAndReply(
              FROM_HERE, kTaskTraits,
              base::BindOnce(IgnoreResult(&base::DeleteFile),
                             crx_operation_result.response),
              base::BindOnce(std::move(callback), result));
        },
        crx_operation_result, std::move(callback));
#endif  // !defined(IN_MEMORY_UPDATES)
#else  // BUILDFLAG(IS_STARBOARD)
    callback = base::BindOnce(
        [](const base::FilePath& crx_file,
           base::OnceCallback<void(const Unpacker::Result&)> callback,
           const Unpacker::Result& result) {
          base::ThreadPool::PostTaskAndReply(
              FROM_HERE, kTaskTraits,
              base::BindOnce(IgnoreResult(&base::DeleteFile), crx_file),
              base::BindOnce(std::move(callback), result));
        },
        crx_file, std::move(callback));
#endif
  }

  // Unpack the file.
  base::ThreadPool::CreateSequencedTaskRunner(kTaskTraits)
      ->PostTask(
          FROM_HERE,
          base::BindOnce(
              &Unpacker::Unpack, pk_hash,
              // If and only if cached, the original path no longer exists.
#if BUILDFLAG(IS_STARBOARD)
              crx_operation_result,
#else
              cache_result.has_value() ? cache_result.value() : crx_file,
#endif
              std::move(unzipper), crx_format,
              base::BindPostTaskToCurrentDefault(std::move(callback))));
}

}  // namespace

base::OnceClosure InstallOperation(
    scoped_refptr<CrxCache> crx_cache,
    std::unique_ptr<Unzipper> unzipper,
    crx_file::VerifierFormat crx_format,
    const std::string& id,
    const std::string& file_hash,
    const std::vector<uint8_t>& pk_hash,
    scoped_refptr<CrxInstaller> installer,
    std::unique_ptr<CrxInstaller::InstallParams> install_params,
#if BUILDFLAG(IS_STARBOARD)
    PersistedData* metadata,
#endif
    base::RepeatingCallback<void(base::Value::Dict)> event_adder,
    base::RepeatingCallback<void(ComponentState)> state_tracker,
    CrxInstaller::ProgressCallback progress_callback,
    base::OnceCallback<void(const CrxInstaller::Result&)>
        installer_result_callback,
#if BUILDFLAG(IS_STARBOARD)
    const OperationResult& crx_operation_result,
    base::OnceCallback<void(base::expected<OperationResult, CategorizedError>)>
#else
    const base::FilePath& crx_file,
    base::OnceCallback<void(base::expected<base::FilePath, CategorizedError>)>
#endif
        callback) {
  state_tracker.Run(ComponentState::kUpdating);
#if BUILDFLAG(IS_STARBOARD)
  Unpack(
      base::BindOnce(
          &Install,
          base::BindOnce(&InstallComplete, std::move(installer_result_callback),
                         std::move(callback), event_adder,
                         crx_operation_result),
          std::move(install_params), installer, progress_callback,
          metadata, id,
          crx_operation_result),
      crx_operation_result, std::move(unzipper), pk_hash, crx_format,
      base::unexpected(UnpackerError::kCrxCacheNotProvided));
#else
  crx_cache->Put(
      // TODO(crbug.com/399617574): Remove FP.
      crx_file, id, file_hash, /*fp=*/{},
      base::BindOnce(
          &Unpack,
          base::BindOnce(
              &Install,
              base::BindOnce(&InstallComplete,
                             std::move(installer_result_callback),
                             std::move(callback), event_adder, crx_file),
              std::move(install_params), installer, progress_callback),
          crx_file, std::move(unzipper), pk_hash, crx_format));
#endif
  return base::DoNothing();
}

}  // namespace update_client
