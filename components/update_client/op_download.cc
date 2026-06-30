// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/op_download.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/update_client/cancellation.h"
#include "components/update_client/component.h"
#include "components/update_client/configurator.h"
#include "components/update_client/crx_downloader.h"
#include "components/update_client/crx_downloader_factory.h"
#include "components/update_client/protocol_definition.h"
#include "components/update_client/task_traits.h"
#include "components/update_client/update_client_errors.h"
#include "components/update_client/update_engine.h"
#include "url/gurl.h"

#if BUILDFLAG(IS_STARBOARD)
#include <algorithm>
#include "starboard/system.h"  // nogncheck
#endif

namespace update_client {

namespace {

#if BUILDFLAG(IS_MAC)
// The minimum size of a download to attempt it at background priority.
constexpr int64_t kBackgroundDownloadSizeThreshold = 10'000'000; /*10 MB*/
#elif !BUILDFLAG(IS_STARBOARD)
constexpr int64_t kBackgroundDownloadSizeThreshold = 0;
#endif

#if defined(IN_MEMORY_UPDATES)
// Memory buffer required for downloading the update in memory.
constexpr int64_t kMemoryBufferBytes = 35 * 1024 * 1024;
#endif

#if !BUILDFLAG(IS_STARBOARD)
bool CanDoBackgroundDownload(bool is_foreground,
                             bool background_downloads_enabled,
                             int64_t size) {
  return !is_foreground && background_downloads_enabled &&
         size >= kBackgroundDownloadSizeThreshold;
}
#endif

// Returns a string literal corresponding to the value of the downloader |d|.
const char* DownloaderToString(CrxDownloader::DownloadMetrics::Downloader d) {
  switch (d) {
    case CrxDownloader::DownloadMetrics::kUrlFetcher:
      return "direct";
    case CrxDownloader::DownloadMetrics::kBits:
      return "bits";
    case CrxDownloader::DownloadMetrics::kBackgroundMac:
      return "nsurlsession_background";
    default:
      return "unknown";
  }
}

base::Value::Dict MakeEvent(const CrxDownloader::DownloadMetrics& dm) {
  base::Value::Dict event;
  event.Set("eventtype", protocol_request::kEventDownload);
  event.Set("eventresult", static_cast<int>(dm.error == 0));
  event.Set("downloader", DownloaderToString(dm.downloader));
  if (dm.error) {
    event.Set("errorcode", dm.error);
  }
  if (dm.extra_code1) {
    event.Set("extracode1", dm.extra_code1);
  }
  event.Set("url", dm.url.spec());

  // -1 means that the  byte counts are not known.
  if (dm.total_bytes >= 0 &&
      dm.total_bytes < protocol_request::kProtocolMaxInt) {
    event.Set("total", static_cast<double>(dm.total_bytes));
  }
  if (dm.downloaded_bytes >= 0 &&
      dm.downloaded_bytes < protocol_request::kProtocolMaxInt) {
    event.Set("downloaded", static_cast<double>(dm.downloaded_bytes));
  }
  if (dm.download_time_ms >= 0 &&
      dm.download_time_ms < protocol_request::kProtocolMaxInt) {
    event.Set("download_time_ms", static_cast<double>(dm.download_time_ms));
  }
  return event;
}

void DownloadComplete(
    scoped_refptr<CrxDownloader> crx_downloader,
    scoped_refptr<Cancellation> cancellation,
    base::RepeatingCallback<void(base::Value::Dict)> event_adder,
#if defined(IN_MEMORY_UPDATES)
    const std::string* crx_str,
#endif
#if BUILDFLAG(IS_STARBOARD)
    base::OnceCallback<void(base::expected<OperationResult, CategorizedError>)>
#else
    base::OnceCallback<void(base::expected<base::FilePath, CategorizedError>)>
#endif
        callback,
    const CrxDownloader::Result& download_result) {
  cancellation->Clear();

  for (const auto& metric : crx_downloader->download_metrics()) {
    event_adder.Run(MakeEvent(metric));
  }

  if (cancellation->IsCancelled()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected<CategorizedError>(
                {.category = ErrorCategory::kService,
                 .code = static_cast<int>(ServiceError::CANCELLED)})));
    return;
  }

  if (download_result.error) {
#if !defined(IN_MEMORY_UPDATES)
    CHECK(download_result.response.empty());
#endif
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback),
                                  base::unexpected<CategorizedError>(
                                      {.category = ErrorCategory::kDownload,
                                       .code = download_result.error,
                                       .extra = download_result.extra_code1})));
    return;
  }
#if BUILDFLAG(IS_STARBOARD)
  OperationResult result;
#if defined(IN_MEMORY_UPDATES)
  result.installation_dir = download_result.installation_dir;
  result.crx_str = crx_str;
#else
  result.response = download_result.response;
#endif
  result.installation_index = download_result.installation_index;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), result));
#else
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), download_result.response));
#endif
}

void HandleAvailableSpace(
    scoped_refptr<Configurator> config,
    scoped_refptr<Cancellation> cancellation,
    bool is_foreground,
    const std::vector<GURL>& urls,
    int64_t size,
    const std::string& hash,
    CrxDownloader::ProgressCallback progress_callback,
    base::RepeatingCallback<void(base::Value::Dict)> event_adder,
#if defined(IN_MEMORY_UPDATES)
    std::string* crx_str,
#endif
#if BUILDFLAG(IS_STARBOARD)
    base::OnceCallback<void(base::expected<OperationResult, CategorizedError>)>
#else
    base::OnceCallback<void(base::expected<base::FilePath, CategorizedError>)>
#endif
        callback,
    int64_t available_bytes) {
  if (available_bytes / 2 <= size) {
    VLOG(1) << "available_bytes: " << available_bytes
            << ", download size: " << size;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected<CategorizedError>(
                {.category = ErrorCategory::kDownload,
                 .code = static_cast<int>(CrxDownloaderError::DISK_FULL)})));
    return;
  }

#if BUILDFLAG(IS_STARBOARD) && defined(IN_MEMORY_UPDATES)
  int64_t total_memory = SbSystemGetTotalCPUMemory();
  int64_t used_memory = SbSystemGetUsedCPUMemory();
  int64_t available_memory = std::max(int64_t{0}, total_memory - used_memory);

  if (total_memory > 0 && available_memory < size + kMemoryBufferBytes) {
    VLOG(1)
        << "Insufficient memory for the update plus 20MB buffer. Available memory: "
        << available_memory << ", download size: " << size;
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            std::move(callback),
            base::unexpected<CategorizedError>(
                {.category = ErrorCategory::kDownload,
                 .code = static_cast<int>(CrxDownloaderError::OUT_OF_MEMORY)})));
    return;
  }
#endif
  scoped_refptr<CrxDownloader> crx_downloader =
#if BUILDFLAG(IS_STARBOARD)
      config->GetCrxDownloaderFactory()->MakeCrxDownloader(config);
#else
      config->GetCrxDownloaderFactory()->MakeCrxDownloader(
          CanDoBackgroundDownload(is_foreground,
                                  config->EnabledBackgroundDownloader(), size));
#endif
  crx_downloader->set_progress_callback(progress_callback);
  cancellation->OnCancel(crx_downloader->StartDownload(
      urls, hash,
#if defined(IN_MEMORY_UPDATES)
      crx_str,
#endif
      base::BindOnce(&DownloadComplete, crx_downloader, cancellation,
#if defined(IN_MEMORY_UPDATES)
                     event_adder, crx_str, std::move(callback))));
#else
                     event_adder, std::move(callback))));
#endif
}

}  // namespace

base::OnceClosure DownloadOperation(
    scoped_refptr<Configurator> config,
    base::RepeatingCallback<int64_t(const base::FilePath&)> get_available_space,
    bool is_foreground,
    const std::vector<GURL>& urls,
    int64_t size,
    const std::string& hash,
    base::RepeatingCallback<void(base::Value::Dict)> event_adder,
    base::RepeatingCallback<void(ComponentState)> state_tracker,
#if defined(IN_MEMORY_UPDATES)
    std::string* crx_str,
#endif
    CrxDownloader::ProgressCallback progress_callback,
#if BUILDFLAG(IS_STARBOARD)
    const OperationResult& file,
    base::OnceCallback<void(base::expected<OperationResult, CategorizedError>)>
#else
    const base::FilePath& file,
    base::OnceCallback<void(base::expected<base::FilePath, CategorizedError>)>
#endif
        callback) {
  state_tracker.Run(ComponentState::kDownloading);
  auto cancellation = base::MakeRefCounted<Cancellation>();
  progress_callback.Run(-1, -1);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, kTaskTraits,
      base::BindOnce(
          [](base::RepeatingCallback<int64_t(const base::FilePath&)>
                 get_available_space) {
            base::ScopedTempDir temp_dir;
            return temp_dir.CreateUniqueTempDir()
                       ? get_available_space.Run(temp_dir.GetPath())
                       : int64_t{0};
          },
          get_available_space),
      base::BindOnce(&HandleAvailableSpace, config, cancellation, is_foreground,
                     urls, size, hash, progress_callback, event_adder,
#if defined(IN_MEMORY_UPDATES)
                     crx_str,
#endif
                     std::move(callback)));
  return base::BindOnce(&Cancellation::Cancel, cancellation);
}

}  // namespace update_client
