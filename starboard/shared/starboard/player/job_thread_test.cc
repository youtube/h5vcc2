// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/player/job_thread.h"

#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "base/memory/cobalt_memory_context.h"
#include "starboard/common/log.h"
#include "starboard/common/time.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {

namespace {

using ::testing::ElementsAre;

TEST(JobThreadTest, PropagatesMediaMemoryContext) {
  auto job_thread = JobThread::Create("MediaJobThread");
  base::memory::MemoryContext context = base::memory::MemoryContext::kUnknown;
  job_thread->ScheduleAndWait(
      [&]() { context = base::memory::GetCurrentMemoryContext(); });
  EXPECT_EQ(context, base::memory::MemoryContext::kMedia);
  job_thread->Stop();
}
// Require at least millisecond-level precision.
constexpr int64_t kPrecisionUsec = 1000;

void ExecutePendingJobs(JobThread* job_thread) {
  job_thread->ScheduleAndWait([]() {});
}

TEST(JobThreadTest, ScheduledJobsAreExecutedInOrder) {
  std::vector<int> values;
  auto job_thread = JobThread::Create("JobThreadTests");
  job_thread->Schedule([&]() { values.push_back(1); });
  job_thread->Schedule([&]() { values.push_back(2); });
  job_thread->Schedule([&]() { values.push_back(3); });
  job_thread->Schedule([&]() { values.push_back(4); }, 1 * kPrecisionUsec);
  job_thread->Schedule([&]() { values.push_back(5); }, 1 * kPrecisionUsec);
  job_thread->Schedule([&]() { values.push_back(6); }, 1 * kPrecisionUsec);
  job_thread->Schedule([&]() { values.push_back(7); }, 2 * kPrecisionUsec);
  job_thread->Schedule([&]() { values.push_back(8); }, 3 * kPrecisionUsec);

  // Sleep past the last scheduled job.
  usleep(4 * kPrecisionUsec);

  ExecutePendingJobs(job_thread.get());

  EXPECT_THAT(values, ElementsAre(1, 2, 3, 4, 5, 6, 7, 8));
  job_thread->Stop();
}

TEST(JobThreadTest, ScheduleAndWaitWaits) {
  int64_t start = CurrentMonotonicTime();
  std::atomic_bool job_1 = {false};
  auto job_thread = JobThread::Create("JobThreadTests");
  job_thread->ScheduleAndWait([&]() {
    usleep(1 * kPrecisionUsec);
    job_1 = true;
  });
  // Verify that the job ran and that it took at least as long as it slept.
  EXPECT_TRUE(job_1);
  EXPECT_GE(CurrentMonotonicTime() - start, 1 * kPrecisionUsec);
  job_thread->Stop();
}

TEST(JobThreadTest, AcceptsMoveOnlyLambda) {
  std::atomic_bool executed = false;
  auto move_only_obj = std::make_unique<int>(100);
  auto job_thread = JobThread::Create("JobThreadTests");

  job_thread->ScheduleAndWait([ptr = std::move(move_only_obj), &executed]() {
    EXPECT_EQ(*ptr, 100);
    executed = true;
  });

  EXPECT_TRUE(executed);
  job_thread->Stop();
}

TEST(JobThreadTest, ScheduledJobsShouldNotExecuteAfterGoingOutOfScope) {
  std::atomic_int counter = {0};
  {
    auto job_thread = JobThread::Create("JobThreadTests");
    std::function<void()> job = [&]() {
      counter++;
      job_thread->Schedule(job, 2 * kPrecisionUsec);
    };
    job_thread->Schedule(job);

    // Wait for the job to run at least once and reschedule itself.
    usleep(1 * kPrecisionUsec);
    ExecutePendingJobs(job_thread.get());
    job_thread->Stop();
  }
  int end_value = counter;
  EXPECT_GE(counter, 1);

  // Sleep past two more (potential) executions and verify there were none.
  usleep(4 * kPrecisionUsec);
  EXPECT_EQ(counter, end_value);
}

TEST(JobThreadTest, CanceledJobsAreCanceled) {
  std::atomic_int counter_1 = {0}, counter_2 = {0};
  JobQueue::JobToken job_token_1 = JobQueue::JobToken::kUnscheduled;
  JobQueue::JobToken job_token_2 = JobQueue::JobToken::kUnscheduled;

  auto job_thread = JobThread::Create("JobThreadTests");
  std::function<void()> job_1 = [&]() {
    counter_1++;
    job_token_1 = job_thread->Schedule(job_1);
  };
  std::function<void()> job_2 = [&]() {
    counter_2++;
    job_token_2 = job_thread->Schedule(job_2);
  };

  job_token_1 = job_thread->Schedule(job_1);
  job_token_2 = job_thread->Schedule(job_2);

  // Wait for the scheduled jobs to at least run once.
  ExecutePendingJobs(job_thread.get());

  // Cancel job 1 and grab the current counter values.
  job_thread->ScheduleAndWait(
      [&]() { job_thread->RemoveJobByToken(&job_token_1); });
  int checkpoint_1 = counter_1;
  int checkpoint_2 = counter_2;

  // Sleep and wait for pending jobs to run.
  usleep(1 * kPrecisionUsec);
  ExecutePendingJobs(job_thread.get());

  // Job 1 should not have run again.
  EXPECT_EQ(counter_1, checkpoint_1);

  // Job 2 should have continued running.
  EXPECT_GT(counter_2, checkpoint_2);

  // Cancel job 2 to avoid it scheduling itself during destruction.
  job_thread->ScheduleAndWait(
      [&]() { job_thread->RemoveJobByToken(&job_token_2); });
  job_thread->Stop();
}

TEST(JobThreadTest, QueueBelongsToCorrectThread) {
  auto job_thread = JobThread::Create("JobThreadTests");
  JobQueue job_queue;

  bool belongs_to_job_thread = false;
  bool belongs_to_main_thread = false;

  // Schedule in JobQueue owned by job thread.
  job_thread->ScheduleAndWait([&]() {
    belongs_to_job_thread = job_thread->BelongsToCurrentThread();
    belongs_to_main_thread = job_queue.BelongsToCurrentThread();
  });

  EXPECT_TRUE(belongs_to_job_thread);
  EXPECT_FALSE(belongs_to_main_thread);

  belongs_to_job_thread = belongs_to_main_thread = false;

  // Schedule in JobQueue owned by main thread.
  job_queue.Schedule([&]() {
    belongs_to_job_thread = job_thread->BelongsToCurrentThread();
    belongs_to_main_thread = job_queue.BelongsToCurrentThread();
  });

  job_queue.RunUntilIdle();
  EXPECT_FALSE(belongs_to_job_thread);
  EXPECT_TRUE(belongs_to_main_thread);
  job_thread->Stop();
}

TEST(JobThreadTest, Stop) {
  std::mutex mutex;
  std::condition_variable cv;
  bool started = false;

  auto job_thread = JobThread::Create("test");

  job_thread->Schedule([&] {
    {
      std::lock_guard lock(mutex);
      started = true;
    }
    cv.notify_one();

    usleep(5'000);

    // Pending task is still running. job_thread should not be null.
    EXPECT_NE(job_thread, nullptr);
  });

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&started] { return started; });
  }

  // Calling Stop() explicitly ensures the pending task completes before
  // 'job_thread' is destroyed at the end of the scope. This ensures that the
  // captured reference remains valid and non-null while the task is running.
  job_thread->Stop();
}

}  // namespace

}  // namespace starboard
