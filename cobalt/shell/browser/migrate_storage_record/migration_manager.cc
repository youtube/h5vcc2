// Copyright 2025 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cobalt/shell/browser/migrate_storage_record/migration_manager.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/base64url.h"
#include "base/command_line.h"
#include "base/containers/adapters.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "cobalt/browser/switches.h"
#include "cobalt/shell/common/shell_switches.h"
#include "components/services/storage/public/mojom/local_storage_control.mojom.h"
#include "components/url_matcher/url_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/schemeful_site.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_access_result.h"
#include "net/cookies/cookie_options.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"
#include "starboard/common/storage.h"
#include "starboard/configuration_constants.h"
#include "starboard/system.h"
#include "third_party/blink/public/common/storage_key/storage_key.h"
#include "third_party/blink/public/mojom/dom_storage/storage_area.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace cobalt {
namespace migrate_storage_record {

using StorageAreaRemote = mojo::Remote<blink::mojom::StorageArea>;

namespace {

// ============================================================================
// Shared Utilities
// ============================================================================

std::string g_migration_status_param;

// A thread-safe, reference-counted wrapper around a base::OnceClosure.
// This is used to ensure a callback is executed exactly once, even if
// multiple async paths (like an IPC response and a timeout) attempt to run it.
struct SharedClosureState : public base::RefCounted<SharedClosureState> {
  base::OnceClosure cb;
  // Keeps the StorageArea Mojo connection alive during the injection process.
  mojo::Remote<blink::mojom::StorageArea> storage_area;

  void Run() {
    if (cb) {
      std::move(cb).Run();
    }
  }

 private:
  friend class base::RefCounted<SharedClosureState>;
  ~SharedClosureState() = default;
};

constexpr char kOutcomeHistogram[] = "Cobalt.StorageMigration.Outcome";
constexpr char kFastPathDurationHistogram[] =
    "Cobalt.StorageMigration.FastPathDuration";
constexpr char kReadResultHistogram[] = "Cobalt.StorageMigration.ReadResult";
constexpr char kCookieInjectionResultHistogram[] =
    "Cobalt.StorageMigration.CookieInjectionResult";
constexpr char kLocalStorageInjectionResultHistogram[] =
    "Cobalt.StorageMigration.LocalStorageInjectionResult";
constexpr char kCookiesCountHistogram[] =
    "Cobalt.StorageMigration.CookiesCount";
constexpr char kLocalStorageCountHistogram[] =
    "Cobalt.StorageMigration.LocalStorageCount";
constexpr char kDurationHistogram[] = "Cobalt.StorageMigration.Duration";
constexpr char kSentinelWriteResultHistogram[] =
    "Cobalt.StorageMigration.SentinelWriteResult";

// ============================================================================
// General File / Cache Utilities
// ============================================================================

// Retrieves the system cache directory used by the legacy Starboard app.
base::FilePath GetOldCachePath() {
  std::vector<char> path(kSbFileMaxPath, 0);
  bool success =
      SbSystemGetPath(kSbSystemPathCacheDirectory, path.data(), path.size());
  if (!success) {
    return base::FilePath();
  }
  return base::FilePath(path.data());
}

// Retrieves the path to the sentinel file used to fast-path skip the migration.
// If this file exists, it means the migration was already completed on a prior
// app launch.
base::FilePath GetMigrationSentinelPath() {
  base::FilePath current_cache_path;
  if (!base::PathService::Get(base::DIR_CACHE, &current_cache_path)) {
    LOG(ERROR) << "Failed to get cache directory for migration sentinel file.";
    return base::FilePath();
  }
  return current_cache_path.Append("migration_completed.txt");
}

// Writes the sentinel file `migration_completed.txt` in the background so that
// future app launches can skip the migration process.
void WriteMigrationSentinelAsync(scoped_refptr<MigrationState> state) {
  base::FilePath sentinel_path = GetMigrationSentinelPath();
  SentinelWriteResult result = SentinelWriteResult::kSuccess;

  if (sentinel_path.empty()) {
    LOG(ERROR) << "Sentinel path is empty, cannot write sentinel file.";
    result = SentinelWriteResult::kEmptyPath;
  } else {
    // WriteFile returns a boolean indicating success.
    // state->GetStatusString() is used as the file content.
    if (!base::WriteFile(sentinel_path, state->GetStatusString())) {
      LOG(ERROR) << "Failed to write migration sentinel file to "
                 << sentinel_path.value();
      result = SentinelWriteResult::kWriteFailed;
    }
  }

  // Record the result to UMA.
  base::UmaHistogramEnumeration(kSentinelWriteResultHistogram, result);
}

// Generates the Base64 encoded key used to look up the legacy Starboard storage
// record for the given URL.
std::string GetApplicationKey(const GURL& url) {
  std::string encoded_url;
  base::Base64UrlEncode(url_matcher::util::Normalize(url).spec(),
                        base::Base64UrlEncodePolicy::INCLUDE_PADDING,
                        &encoded_url);
  return encoded_url;
}

// Deletes the C25 Starboard Storage Record file from disk.
// Returns true if the deletion was successful or the record was already
// invalid.
bool DeleteLegacyStorageRecord(starboard::StorageRecord* record) {
  if (!record || !record->IsValid()) {
    return true;  // Nothing to delete if null or not valid.
  }
  if (!record->Delete()) {
    LOG(ERROR) << "Failed to delete legacy storage record.";
    return false;
  }
  return true;
}

// Deletes the legacy starboard storage files asynchronously.
void DeleteLegacyStorageFilesAsync() {
  // Logic extracted from the internal lambda.
  GURL initial_url(
      switches::GetInitialURL(*base::CommandLine::ForCurrentProcess()));

  if (initial_url.is_valid()) {
    std::string partition_key = GetApplicationKey(initial_url);
    auto record =
        std::make_unique<starboard::StorageRecord>(partition_key.c_str());
    DeleteLegacyStorageRecord(record.get());
  }

  // Also attempt to delete the fallback default record.
  auto fallback_record = std::make_unique<starboard::StorageRecord>();
  DeleteLegacyStorageRecord(fallback_record.get());
}

// Deletes legacy cache subdirectories asynchronously to free up disk space.
// It ensures that it does not accidentally delete the current actively used
// cache directory.
void DeleteOldCacheDirectoryAsync() {
  base::FilePath old_cache_path = GetOldCachePath();
  if (old_cache_path.empty() || !base::PathExists(old_cache_path)) {
    return;
  }

  base::FilePath current_cache_path;
  if (!base::PathService::Get(base::DIR_CACHE, &current_cache_path)) {
    LOG(ERROR) << "Failed to get current cache directory. Skipping "
                  "deletion of old cache path.";
    return;
  }

  // Safety check: Don't delete the directory if it's currently in use.
  if (!old_cache_path.IsParent(current_cache_path) &&
      old_cache_path != current_cache_path) {
    if (!base::DeletePathRecursively(old_cache_path)) {
      LOG(ERROR) << "Failed to delete old cache directory: "
                 << old_cache_path.value();
    }
    return;
  }

  // If the paths overlap, we delete only specific legacy subdirectories.
  const std::vector<std::string> kOldCacheSubpaths = {
      "cache_settings.json",
      "compiled_js",
      "css",
      "font",
      "html",
      "image",
      "other",
      "service_worker_settings.json",
      "settings.json",
      "splash",
      "splash_screen",
      "uncompiled_js",
  };

  for (const auto& subpath : kOldCacheSubpaths) {
    base::FilePath old_cache_subpath = old_cache_path.Append(subpath);
    if (base::PathExists(old_cache_subpath)) {
      if (!base::DeletePathRecursively(old_cache_subpath)) {
        LOG(ERROR) << "Failed to delete old cache subpath: "
                   << old_cache_subpath.value();
      }
    }
  }
}

void SetMigrationStatusUrlParameter(scoped_refptr<MigrationState> state) {
  g_migration_status_param = "migration_status=" + state->GetStatusString();
}

// Triggers the deletion of old cache directories, deletes the legacy storage
// files, and writes the migration sentinel file.
void CleanupLegacyFilesAsync(scoped_refptr<MigrationState> state) {
  base::UmaHistogramEnumeration(kOutcomeHistogram, state->GetOutcome());
  SetMigrationStatusUrlParameter(state);

  // M138 Style: Use direct PostTask with explicit power traits.
  // We don't need a SequencedTaskRunner because these three functions
  // can actually run in parallel or on any available worker.
  auto traits =
      base::TaskTraits{base::MayBlock(), base::TaskPriority::BEST_EFFORT,
                       base::ThreadPolicy::PREFER_BACKGROUND,
                       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN};
  base::ThreadPool::PostTask(FROM_HERE, traits,
                             base::BindOnce(&DeleteOldCacheDirectoryAsync));
  base::ThreadPool::PostTask(FROM_HERE, traits,
                             base::BindOnce(&DeleteLegacyStorageFilesAsync));
  base::ThreadPool::PostTask(
      FROM_HERE, traits, base::BindOnce(&WriteMigrationSentinelAsync, state));
}

// Creates a task that runs the legacy files cleanup and then immediately
// advances the migration pipeline.
Task CleanupLegacyFilesTask(scoped_refptr<MigrationState> state) {
  return base::BindOnce(
      [](scoped_refptr<MigrationState> state, base::OnceClosure callback) {
        CleanupLegacyFilesAsync(state);
        std::move(callback).Run();
      },
      state);
}

// Emits UMA metrics for the total duration of the migration process.
Task RecordMigrationDurationTask(
    std::unique_ptr<base::ElapsedTimer> elapsed_timer) {
  return base::BindOnce(
      [](std::unique_ptr<base::ElapsedTimer> elapsed_timer,
         base::OnceClosure callback) {
        base::TimeDelta elapsed = elapsed_timer->Elapsed();
        base::UmaHistogramTimes(kDurationHistogram, elapsed);
        LOG(INFO) << "Cookie and localStorage migration completed in "
                  << elapsed.InMilliseconds() << " ms.";
        std::move(callback).Run();
      },
      std::move(elapsed_timer));
}

// ============================================================================
// Legacy Storage Reader
// ============================================================================

// Reads the legacy Starboard storage record from disk and parses it into a
// cobalt::storage::Storage protobuf, alongside the final StorageReadResult.
std::pair<StorageReadResult, std::unique_ptr<cobalt::storage::Storage>>
ReadAndParseLegacyStorageRecord() {
  // Legacy Storage records start with the header 'SAV1'.
  constexpr char kRecordHeader[] = "SAV1";
  constexpr size_t kRecordHeaderSize = 4;

  StorageReadResult result = StorageReadResult::kSuccess;
  std::unique_ptr<cobalt::storage::Storage> storage;

  [&]() {
    GURL initial_url(
        switches::GetInitialURL(*base::CommandLine::ForCurrentProcess()));
    if (!initial_url.is_valid()) {
      result = StorageReadResult::kRecordInvalid;
      return;
    }

    std::string partition_key = GetApplicationKey(initial_url);
    auto record =
        std::make_unique<starboard::StorageRecord>(partition_key.c_str());
    if (!record->IsValid()) {
      bool fallback =
          partition_key == GetApplicationKey(GURL(::switches::kDefaultURL));
      if (!fallback) {
        result = StorageReadResult::kPartitionKeyNotDefault;
        return;
      }

      record = std::make_unique<starboard::StorageRecord>();
      if (!record->IsValid()) {
        result = StorageReadResult::kRecordInvalid;
        return;
      }
    }

    if (record->GetSize() < static_cast<int64_t>(kRecordHeaderSize)) {
      result = StorageReadResult::kSizeTooSmall;
      return;
    }

    auto bytes = std::vector<uint8_t>(record->GetSize());
    const int read_result =
        record->Read(reinterpret_cast<char*>(bytes.data()), bytes.size());

    if (read_result < 0 || static_cast<size_t>(read_result) != bytes.size()) {
      result = StorageReadResult::kReadMismatch;
      return;
    }

    std::string version(reinterpret_cast<const char*>(bytes.data()),
                        kRecordHeaderSize);
    if (version != kRecordHeader) {
      result = StorageReadResult::kHeaderMismatch;
      return;
    }

    storage = std::make_unique<cobalt::storage::Storage>();
    if (!storage->ParseFromArray(
            reinterpret_cast<const char*>(bytes.data() + kRecordHeaderSize),
            bytes.size() - kRecordHeaderSize)) {
      result = StorageReadResult::kParseError;
      storage.reset();
      return;
    }
  }();

  if (storage) {
    LOG(INFO) << "Successfully parsed storage. Cookies: "
              << storage->cookies_size()
              << ", LocalStorage groups: " << storage->local_storages_size();
  } else {
    LOG(INFO) << "Legacy storage was not found or failed to parse.";
  }
  return {result, std::move(storage)};
}

// ============================================================================
// Cookie Migration Utilities
// ============================================================================

// Retrieves the CookieManager for the given StoragePartition.
network::mojom::CookieManager* GetCookieManager(
    content::StoragePartition* partition) {
  return partition->GetCookieManagerForBrowserProcess();
}

// ============================================================================
// LocalStorage Migration Utilities
// ============================================================================

// Binds a mojo remote to the StorageArea for the given origin, allowing direct
// write access to the DOM Storage subsystem.
void GetLocalStorageArea(content::StoragePartition* partition,
                         const url::Origin& origin,
                         mojo::Remote<blink::mojom::StorageArea>& out_area) {
  auto storage_key = blink::StorageKey::CreateFirstParty(origin);
  partition->GetLocalStorageControl()->BindStorageArea(
      storage_key, out_area.BindNewPipeAndPassReceiver());
}

// Formats a string to match the internal binary layout expected by the
// Chromium LocalStorage database backend (LevelDB).
std::vector<uint8_t> FormatStringForLocalStorage(const std::string& input) {
  std::u16string utf16_str = base::UTF8ToUTF16(input);
  bool is_latin1 = true;
  for (char16_t c : utf16_str) {
    if (c > 0xFF) {
      is_latin1 = false;
      break;
    }
  }

  std::vector<uint8_t> result;
  if (is_latin1) {
    result.reserve(utf16_str.size() + 1);
    result.push_back(1);  // StorageFormat::Latin1
    for (char16_t c : utf16_str) {
      result.push_back(static_cast<uint8_t>(c));
    }
  } else {
    result.reserve(utf16_str.size() * sizeof(char16_t) + 1);
    result.push_back(0);  // StorageFormat::UTF16
    const uint8_t* chars = reinterpret_cast<const uint8_t*>(utf16_str.data());
    result.insert(result.end(), chars,
                  chars + utf16_str.size() * sizeof(char16_t));
  }
  return result;
}

}  // namespace

// static
std::string MigrationManager::GetMigrationStatusUrlParameter() {
  return g_migration_status_param;
}

// static
void MigrationManager::ResetMigrationStatusForTesting() {
  g_migration_status_param.clear();
}

// static
// Groups multiple sequential asynchronous tasks into a single chain of
// callbacks. When the returned Task is invoked, it will execute the first task,
// which in turn will trigger the second, and so on, until all tasks are run.
Task MigrationManager::GroupTasks(std::vector<Task> tasks) {
  return base::BindOnce(
      [](std::vector<Task> tasks, base::OnceClosure callback) {
        base::OnceClosure all_tasks_closure = std::move(callback);
        for (Task& task : base::Reversed(tasks)) {
          all_tasks_closure =
              base::BindOnce(std::move(task), std::move(all_tasks_closure));
        }
        std::move(all_tasks_closure).Run();
      },
      std::move(tasks));
}

// Executes the entire one-time migration pipeline.
// If the migration has already completed on a previous launch (determined by
// the presence of a sentinel file), this function instantly invokes the
// done_callback to resume app startup immediately.
void MigrationManager::RunMigration(content::StoragePartition* partition,
                                    base::OnceClosure done_callback) {
  LOG(INFO) << "Starting Pre-MainLoop Migration.";

  // TODO: re-enable this.
  // base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  // if (command_line->HasSwitch(switches::kDisableStorageMigration)) {
  //   LOG(INFO) << "Storage migration disabled via switch.";
  //   std::move(done_callback).Run();
  //   return;
  // }

  auto elapsed_timer = std::make_unique<base::ElapsedTimer>();

  // Fast Path: If the sentinel file exists, migration is already done.
  base::ElapsedTimer sentinel_timer;
  base::FilePath sentinel_path = GetMigrationSentinelPath();
  bool sentinel_exists =
      !sentinel_path.empty() && base::PathExists(sentinel_path);
  if (sentinel_exists) {
    LOG(INFO) << "Migration sentinel file found. Skipping migration.";
    LOG(INFO) << "RunMigration fast path took "
              << elapsed_timer->Elapsed().InMilliseconds() << " ms.";
    base::UmaHistogramTimes(kFastPathDurationHistogram,
                            elapsed_timer->Elapsed());
    base::UmaHistogramEnumeration(kOutcomeHistogram,
                                  MigrationOutcome::kFastPath);
    std::move(done_callback).Run();
    return;
  }

  // Attempt to read the legacy storage protobuf from disk.
  GURL initial_url(
      switches::GetInitialURL(*base::CommandLine::ForCurrentProcess()));
  if (!initial_url.is_valid()) {
    LOG(INFO) << "RunMigration invalid URL early exit.";
    std::move(done_callback).Run();
    return;
  }
  url::Origin origin = url::Origin::Create(initial_url);

  auto parsed_storage = ReadAndParseLegacyStorageRecord();
  StorageReadResult read_result = parsed_storage.first;
  auto storage = std::move(parsed_storage.second);
  base::UmaHistogramEnumeration(kReadResultHistogram, read_result);

  auto state = base::MakeRefCounted<MigrationState>();
  state->read_result = read_result;
  state->has_data_to_migrate = storage && (storage->cookies_size() > 0 ||
                                           storage->local_storages_size() > 0);

  if (!state->has_data_to_migrate) {
    LOG(INFO) << "Nothing to migrate.";
    CleanupLegacyFilesAsync(state);
    std::move(done_callback).Run();
    return;
  }

  base::UmaHistogramCounts1000(kCookiesCountHistogram, storage->cookies_size());
  base::UmaHistogramCounts1000(kLocalStorageCountHistogram,
                               storage->local_storages_size());

  LOG(INFO) << "RunMigration synchronous setup took "
            << elapsed_timer->Elapsed().InMilliseconds() << " ms.";

  // Build the pipeline of tasks to execute asynchronously.
  std::vector<Task> tasks;
  // Run LocalStorageTask first. In M138, there is no callback for the flush()
  // API. It is not garanteed it will be finished before Kabuki loads.
  tasks.push_back(CookieTask(partition, ToCanonicalCookies(*storage), state));
  tasks.push_back(LocalStorageTask(
      partition, origin, ToLocalStorageItems(origin, *storage), state));
  tasks.push_back(RecordMigrationDurationTask(std::move(elapsed_timer)));
  tasks.push_back(CleanupLegacyFilesTask(state));

  // Execute the chained tasks. The done_callback will be invoked when all
  // steps, including async background work, have completed.
  std::move(GroupTasks(std::move(tasks))).Run(std::move(done_callback));
}

// static
// Converts the legacy protobuf format for cookies into Chromium's
// CanonicalCookie format.
std::vector<std::unique_ptr<net::CanonicalCookie>>
MigrationManager::ToCanonicalCookies(const cobalt::storage::Storage& storage) {
  std::vector<std::unique_ptr<net::CanonicalCookie>> cookies;
  for (auto& c : storage.cookies()) {
    // CRITICAL for M138: SameSite=None MUST be Secure.
    // We force is_secure to true because legacy Cobalt cookies
    // often lack this bit but require cross-site access.
    bool is_secure = true;

    base::Time creation_time =
        base::Time::FromInternalValue(c.creation_time_us());
    base::Time expiration_time =
        base::Time::FromInternalValue(c.expiration_time_us());

    // Legacy Cobalt cookies might have arbitrarily high expiration dates.
    // Cap the expiration date at the Chromium limit to pass the
    // DCHECK(cookie->IsCanonical()) check during
    // CookieManager::SetCanonicalCookie.
    expiration_time = net::CanonicalCookie::ValidateAndAdjustExpiryDate(
        expiration_time, creation_time, net::CookieSourceScheme::kSecure);

    auto cookie = net::CanonicalCookie::FromStorage(
        c.name(), c.value(), c.domain(), c.path(), creation_time,
        expiration_time, base::Time::FromInternalValue(c.last_access_time_us()),
        creation_time, is_secure, c.http_only(),
        net::CookieSameSite::NO_RESTRICTION,  // SameSite=None
        net::COOKIE_PRIORITY_DEFAULT,
        std::nullopt,  // PartitionKey
        // Since we use https:// in CookieTask, use kSecure here.
        net::CookieSourceScheme::kSecure, 443, net::CookieSourceType::kOther,
        net::CanonicalCookieFromStorageCallSite::kCookieManager);

    if (cookie) {
      if (cookie->IsCanonical()) {
        cookies.push_back(std::move(cookie));
      } else {
        LOG(ERROR) << "Cookie failed IsCanonical() check: "
                   << cookie->DebugString();
      }
    } else {
      LOG(ERROR) << "Failed to create CanonicalCookie from legacy "
                    "storage for: "
                 << "name=" << c.name() << " domain=" << c.domain()
                 << " path=" << c.path();
    }
  }
  return cookies;
}

// static
// Returns an asynchronous task that injects all legacy cookies into the new
// Chromium Network Service.

// Timeout Behavior Summary:
// If a 2-second timeout fires during CookieTask, this state guarantees the
// application startup callback is executed immediately to prevent blocking.
// The Mojo IPC connections are kept alive by the pending callbacks.
// - If the background thread eventually succeeds: the data is successfully
//   written to disk, UMA metrics are recorded, but this state swallows the
//   late callback (a no-op) to prevent running the startup callback twice.
//   The MigrationState ignores the late success, remaining as 'kTimeout'.
// - If the background thread hangs indefinitely: the thread remains blocked,
//   consuming resources or preventing further storage operations. The closures
//   and Mojo pipes are leaked for the lifetime of the application, but the
//   main thread remains unblocked and the application continues running.
Task MigrationManager::CookieTask(
    content::StoragePartition* partition,
    std::vector<std::unique_ptr<net::CanonicalCookie>> cookies,
    scoped_refptr<MigrationState> state) {
  return base::BindOnce(
      [](content::StoragePartition* partition,
         std::vector<std::unique_ptr<net::CanonicalCookie>> cookies,
         scoped_refptr<MigrationState> state, base::OnceClosure callback) {
        if (cookies.empty()) {
          std::move(callback).Run();
          return;
        }

        auto* cookie_manager = GetCookieManager(partition);
        auto shared_state = base::MakeRefCounted<SharedClosureState>();
        shared_state->cb = std::move(callback);

        // Success Path: The barrier executes the callback when all cookies are
        // injected.
        base::RepeatingClosure barrier = base::BarrierClosure(
            cookies.size(), base::BindOnce(
                                [](scoped_refptr<SharedClosureState> state) {
                                  LOG(INFO) << "Cookie injection complete.";
                                  state->Run();
                                },
                                shared_state));

        for (auto& cookie : cookies) {
          std::string name = cookie->Name();

          // M138/Network Service: Ensure the source_url is a full GURL
          // representing the cookie's domain to satisfy Schemeful Site checks.
          std::string host = cookie->Domain();
          if (!host.empty() && host[0] == '.') {
            host = host.substr(1);
          }
          GURL source_url("https://" + host + cookie->Path());

          // M138 Change: Explicitly set the SameSite Context to Inclusive.
          // This prevents IsSetPermittedInContext from crashing when it sees
          // a legacy cookie that doesn't match strict modern site rules.
          net::CookieOptions options = net::CookieOptions::MakeAllInclusive();
          options.set_same_site_cookie_context(
              net::CookieOptions::SameSiteCookieContext::MakeInclusive());

          // Mojo IPC call to the Network Service.
          cookie_manager->SetCanonicalCookie(
              *cookie, source_url, options,
              base::BindOnce(
                  [](base::RepeatingClosure barrier, std::string cookie_name,
                     scoped_refptr<MigrationState> state,
                     net::CookieAccessResult result) {
                    if (!result.status.IsInclude()) {
                      LOG(ERROR)
                          << "SetCanonicalCookie failed for: " << cookie_name
                          << " with status: " << result.status.GetDebugString();
                    }

                    InjectionResult inj_result = result.status.IsInclude()
                                                     ? InjectionResult::kSuccess
                                                     : InjectionResult::kError;

                    // Restored UMA Histograms
                    base::UmaHistogramEnumeration(
                        kCookieInjectionResultHistogram, inj_result);

                    state->UpdateCookieResult(inj_result);
                    barrier.Run();
                  },
                  barrier, name, state));
        }

        // Hard timeout fallback: guarantee app startup resumes after 2 seconds.
        base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(
                [](scoped_refptr<SharedClosureState> shared_state,
                   scoped_refptr<MigrationState> migration_state) {
                  if (shared_state->cb) {
                    LOG(ERROR) << "Cookie injection timed out after 2 "
                                  "seconds. Proceeding anyway.";
                    migration_state->UpdateCookieResult(
                        InjectionResult::kTimeout);
                    shared_state->Run();
                  }
                },
                shared_state, state),
            base::Seconds(2));
      },
      partition, std::move(cookies), state);
}

// static
// Filters the legacy protobuf to retrieve only the LocalStorage key-value pairs
// belonging to the primary URL origin.
std::vector<std::unique_ptr<std::pair<std::string, std::string>>>
MigrationManager::ToLocalStorageItems(const url::Origin& page_origin,
                                      const cobalt::storage::Storage& storage) {
  std::vector<std::unique_ptr<std::pair<std::string, std::string>>> entries;
  for (const auto& local_storages : storage.local_storages()) {
    GURL local_storage_origin(local_storages.serialized_origin());
    if (!local_storage_origin.SchemeIs("https") ||
        !page_origin.IsSameOriginWith(local_storage_origin)) {
      continue;
    }

    for (const auto& local_storage_entry :
         local_storages.local_storage_entries()) {
      entries.emplace_back(new std::pair<std::string, std::string>(
          local_storage_entry.key(), local_storage_entry.value()));
    }
  }
  LOG(INFO) << "ToLocalStorageItems complete. Count=" << entries.size();
  return entries;
}

// static
// Returns an asynchronous task that injects all legacy LocalStorage key-value
// pairs into the Chromium Storage Service. This is executed as a multi-step
// sequence:
// 1. Send all Put commands over Mojo.
// 2. Call Flush() to ensure LocalStorage key-value pairs are in memory.
Task MigrationManager::LocalStorageTask(
    content::StoragePartition* partition,
    const url::Origin& origin,
    std::vector<std::unique_ptr<std::pair<std::string, std::string>>> pairs,
    scoped_refptr<MigrationState> state) {
  return base::BindOnce(
      [](content::StoragePartition* partition, url::Origin origin,
         std::vector<std::unique_ptr<std::pair<std::string, std::string>>>
             pairs,
         scoped_refptr<MigrationState> state, base::OnceClosure callback) {
        if (pairs.empty()) {
          LOG(INFO) << "No LocalStorage pairs, finishing.";
          std::move(callback).Run();
          return;
        }

        // Shared context to keep the mojo::Remote alive until the entire
        // pipeline finishes.
        auto shared_state = base::MakeRefCounted<SharedClosureState>();
        shared_state->cb = std::move(callback);

        GetLocalStorageArea(partition, origin, shared_state->storage_area);

        // STEP 2: Flush step.
        // Tells the Storage Service to commit the LevelDB transactions to disk.
        // We use BindOnce with shared_state to ensure it is kept alive.
        base::OnceClosure flush_step = base::BindOnce(
            [](content::StoragePartition* partition,
               scoped_refptr<MigrationState> state,
               scoped_refptr<SharedClosureState> shared_state) {
              LOG(INFO) << "LocalStorage Puts complete. Flushing...";
              partition->GetLocalStorageControl()->Flush();

              // Proceed immediately. Flush() is non-blocking and we don't need
              // to wait for disk IO to complete before starting the app.
              shared_state->Run();
            },
            base::Unretained(partition), state, shared_state);

        // STEP 1: Put keys step.
        // Iterates through all k/v pairs and sends Mojo Put commands.
        // The `flush_step` runs only when the barrier closes (all Puts have
        // responded).
        base::RepeatingClosure barrier =
            base::BarrierClosure(pairs.size(), std::move(flush_step));

        for (const auto& pair : pairs) {
          std::string key_str = pair->first;
          std::vector<uint8_t> key = FormatStringForLocalStorage(pair->first);
          std::vector<uint8_t> value =
              FormatStringForLocalStorage(pair->second);

          LOG(INFO) << "Put for key: " << key_str;
          shared_state->storage_area->Put(
              key, value, std::nullopt, "migration",
              base::BindOnce(
                  [](base::RepeatingClosure barrier, std::string key_str,
                     scoped_refptr<MigrationState> state,
                     scoped_refptr<SharedClosureState> keep_alive,
                     bool success) {
                    if (success) {
                      LOG(INFO) << "Put SUCCESS for key: " << key_str;
                    } else {
                      LOG(ERROR) << "Put FAILED for key: " << key_str;
                    }
                    InjectionResult inj_result = success
                                                     ? InjectionResult::kSuccess
                                                     : InjectionResult::kError;
                    base::UmaHistogramEnumeration(
                        kLocalStorageInjectionResultHistogram, inj_result);
                    state->UpdateLocalStorageResult(inj_result);
                    barrier.Run();
                  },
                  barrier, key_str, state, shared_state));
        }
      },
      partition, origin, std::move(pairs), state);
}

}  // namespace migrate_storage_record
}  // namespace cobalt
