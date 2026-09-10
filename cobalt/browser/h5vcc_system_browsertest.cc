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

#include "cobalt/testing/browser_tests/browser/test_shell.h"
#include "cobalt/testing/browser_tests/content_browser_test.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace cobalt {

class H5vccSystemBrowserTest : public content::ContentBrowserTest {
 public:
  H5vccSystemBrowserTest() = default;
  ~H5vccSystemBrowserTest() override = default;
};

IN_PROC_BROWSER_TEST_F(H5vccSystemBrowserTest, VerifyApiPresence) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  // Verify that window.h5vcc exists.
  EXPECT_TRUE(content::EvalJs(shell()->web_contents(),
                              "typeof window.h5vcc !== 'undefined'")
                  .ExtractBool());

  // Verify that window.h5vcc.system exists.
  EXPECT_TRUE(content::EvalJs(shell()->web_contents(),
                              "typeof window.h5vcc.system !== 'undefined'")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(H5vccSystemBrowserTest, VerifySystemProperties) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  // Verify advertisingId is a string (might be empty, but should be a string).
  EXPECT_TRUE(
      content::EvalJs(shell()->web_contents(),
                      "typeof window.h5vcc.system.advertisingId === 'string'")
          .ExtractBool());

  // Verify limitAdTracking is a boolean.
  EXPECT_TRUE(content::EvalJs(
                  shell()->web_contents(),
                  "typeof window.h5vcc.system.limitAdTracking === 'boolean'")
                  .ExtractBool());

  // Verify userOnExitStrategy is one of the expected numbers.
  // 0: close, 1: minimize, 2: no-exit
  std::string check_strategy = R"(
    (function() {
      const strategy = window.h5vcc.system.userOnExitStrategy;
      return strategy === 0 || strategy === 1 || strategy === 2;
    })()
  )";
  EXPECT_TRUE(
      content::EvalJs(shell()->web_contents(), check_strategy).ExtractBool());
}

IN_PROC_BROWSER_TEST_F(H5vccSystemBrowserTest, VerifyWasLowMemoryKilled) {
  ASSERT_TRUE(embedded_test_server()->Start());
  GURL url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(NavigateToURL(shell()->web_contents(), url));

  // Verify that wasLowMemoryKilled() exists and returns a Promise.
  EXPECT_TRUE(
      content::EvalJs(
          shell()->web_contents(),
          "typeof window.h5vcc.system.wasLowMemoryKilled === 'function'")
          .ExtractBool());
  EXPECT_TRUE(content::EvalJs(
                  shell()->web_contents(),
                  "window.h5vcc.system.wasLowMemoryKilled() instanceof Promise")
                  .ExtractBool());

  // Verify that the promise resolves to a boolean.
  EXPECT_TRUE(content::EvalJs(
                  shell()->web_contents(),
                  "(async () => typeof (await "
                  "window.h5vcc.system.wasLowMemoryKilled()) === 'boolean')()")
                  .ExtractBool());
}

}  // namespace cobalt
