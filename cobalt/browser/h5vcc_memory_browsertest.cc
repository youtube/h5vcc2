// Copyright 2026 The Cobalt Authors. All Rights Reserved.
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

#include "base/run_loop.h"
#include "cobalt/browser/h5vcc_memory/low_memory_manager.h"
#include "cobalt/testing/browser_tests/browser/test_shell.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace cobalt {

class H5vccMemoryBrowserTest : public content::ContentBrowserTest {
 public:
  H5vccMemoryBrowserTest() = default;
  ~H5vccMemoryBrowserTest() override = default;
};

IN_PROC_BROWSER_TEST_F(H5vccMemoryBrowserTest, VerifyApiPresence) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  // 1. Verify that window.h5vcc exists.
  EXPECT_TRUE(content::EvalJs(shell()->web_contents(),
                              "typeof window.h5vcc !== 'undefined'")
                  .ExtractBool());

  // 2. Verify that window.h5vcc.memory exists.
  EXPECT_TRUE(content::EvalJs(shell()->web_contents(),
                              "typeof window.h5vcc.memory !== 'undefined'")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(H5vccMemoryBrowserTest, LowMemoryEventListener) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  base::RunLoop run_loop;
  browser::LowMemoryManager::GetInstance()
      ->SetOnListenerAddedCallbackForTesting(run_loop.QuitClosure());

  // Set up event listener in JavaScript.
  EXPECT_TRUE(content::ExecJs(shell()->web_contents(), R"(
    window.lowMemoryReceived = 0;
    window.h5vcc.memory.addEventListener('lowmemory', (event) => {
      if (event && event.type === 'lowmemory') {
        window.lowMemoryReceived++;
      }
    });
  )"));

  if (browser::LowMemoryManager::GetInstance()->num_listeners() == 0) {
    run_loop.Run();
  }
  browser::LowMemoryManager::GetInstance()
      ->SetOnListenerAddedCallbackForTesting(base::RepeatingClosure());

  // Trigger low memory event from browser process.
  browser::LowMemoryManager::GetInstance()->OnLowMemory();

  // Wait and verify the JS event listener was invoked.
  EXPECT_EQ(1, content::EvalJs(shell()->web_contents(), R"(
    new Promise((resolve) => {
      if (window.lowMemoryReceived > 0) {
        resolve(window.lowMemoryReceived);
        return;
      }
      const interval = setInterval(() => {
        if (window.lowMemoryReceived > 0) {
          clearInterval(interval);
          resolve(window.lowMemoryReceived);
        }
      }, 50);
    });
  )"));
}

IN_PROC_BROWSER_TEST_F(H5vccMemoryBrowserTest, LowMemoryOnAttributeHandler) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  base::RunLoop run_loop;
  browser::LowMemoryManager::GetInstance()
      ->SetOnListenerAddedCallbackForTesting(run_loop.QuitClosure());

  // Set up onlowmemory handler property in JavaScript.
  EXPECT_TRUE(content::ExecJs(shell()->web_contents(), R"(
    window.onLowMemoryHandled = 0;
    window.h5vcc.memory.onlowmemory = (event) => {
      if (event && event.type === 'lowmemory') {
        window.onLowMemoryHandled++;
      }
    };
  )"));

  if (browser::LowMemoryManager::GetInstance()->num_listeners() == 0) {
    run_loop.Run();
  }
  browser::LowMemoryManager::GetInstance()
      ->SetOnListenerAddedCallbackForTesting(base::RepeatingClosure());

  // Trigger low memory event from browser process.
  browser::LowMemoryManager::GetInstance()->OnLowMemory();

  // Wait and verify the onlowmemory handler was invoked.
  EXPECT_EQ(1, content::EvalJs(shell()->web_contents(), R"(
    new Promise((resolve) => {
      if (window.onLowMemoryHandled > 0) {
        resolve(window.onLowMemoryHandled);
        return;
      }
      const interval = setInterval(() => {
        if (window.onLowMemoryHandled > 0) {
          clearInterval(interval);
          resolve(window.onLowMemoryHandled);
        }
      }, 50);
    });
  )"));
}

}  // namespace cobalt
