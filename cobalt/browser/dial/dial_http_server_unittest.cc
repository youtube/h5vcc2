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

#include "cobalt/browser/dial/dial_http_server.h"

#include <optional>
#include <utility>

#include "base/memory/weak_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/task_environment.h"
#include "cobalt/browser/dial/dial_device_description.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/http/http_status_code.h"
#include "net/server/http_server_response_info.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace in_app_dial {

using ::testing::_;

constexpr net::NetworkTrafficAnnotationTag kNetworkTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("dial_http_server_test",
                                        "dial_http_server_test");

class MockRequestHandler : public DialHttpServer::RequestHandler {
 public:
  MOCK_METHOD(
      void,
      HandleRequest,
      (const std::string& method,
       const std::string& service_name,
       const std::string& path,
       const std::string& data,
       const std::string& host_with_port,
       base::OnceCallback<void(std::optional<net::HttpServerResponseInfo>)>),
      (override));

  base::WeakPtr<MockRequestHandler> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  // Shorthand for invoking the callback argument of HandleRequest() as an
  // action without having to specify its argument index.
  template <typename... RunArgs>
  auto RunCallbackAction(RunArgs&&... run_args) {
    return base::test::RunOnceCallback<kHandleRequestCallbackPosition>(
        std::forward<RunArgs>(run_args)...);
  }

 private:
  // The 0-based position of the callback argument in HandleRequest().
  static constexpr size_t kHandleRequestCallbackPosition = 5;

  base::WeakPtrFactory<MockRequestHandler> weak_ptr_factory_{this};
};

// Test fixture
class DialHttpServerTest : public ::testing::Test {
 public:
  DialHttpServerTest()
      : device_description_(/*uuid=*/"01020304-0506-aabbccdd",
                            /*friendly_name=*/"Friendly Device Name",
                            /*manufacturer_name=*/"Manufacturer Name",
                            /*model_name=*/"Model Name") {}

  void SetUp() override {
    handler_ = std::make_unique<MockRequestHandler>();
    server_ = std::make_unique<DialHttpServer>(device_description_,
                                               handler_->GetWeakPtr());
    server_address_ = server_->GetLocalAddress().ToString();
    ASSERT_FALSE(server_address_.empty());

    auto builder = net::CreateTestURLRequestContextBuilder();
    context_ = builder->Build();
  }

  void TearDown() override { server_.reset(); }

  net::HttpServerResponseInfo CreateEmptyResponse() {
    net::HttpServerResponseInfo response;
    response.SetBody(std::string(), "text/plain");
    return response;
  }

 protected:
  base::test::TaskEnvironment scoped_task_env_{
      base::test::TaskEnvironment::MainThreadType::IO};

  const DialDeviceDescription device_description_;

  std::unique_ptr<MockRequestHandler> handler_;
  std::unique_ptr<DialHttpServer> server_;
  std::string server_address_;

  std::unique_ptr<net::URLRequestContext> context_;
};

TEST_F(DialHttpServerTest, SendManifest) {
  const GURL url(base::StringPrintf("http://%s/dd.xml", server_address_));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->Start();
  delegate.RunUntilComplete();

  EXPECT_EQ(net::OK, delegate.request_status());
  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_OK, request->response_headers()->response_code());
  EXPECT_TRUE(request->response_headers()->HasHeaderValue(
      "APPLICATION-URL",
      base::StringPrintf("http://%s/apps/", server_address_)));
  std::string mime_type;
  std::string charset;
  request->response_headers()->GetMimeTypeAndCharset(&mime_type, &charset);
  EXPECT_EQ("text/xml", mime_type);
  EXPECT_EQ("utf-8", charset);
  EXPECT_FALSE(mime_type.empty());
  EXPECT_FALSE(charset.empty());
  EXPECT_EQ(device_description_.AsXml(), delegate.data_received());
}

TEST_F(DialHttpServerTest, CurrentRunningAppRedirect) {
  EXPECT_CALL(*handler_,
              HandleRequest("GET", "YouTube", "/", std::string(), _, _))
      .WillOnce(handler_->RunCallbackAction(CreateEmptyResponse()));

  const GURL url(base::StringPrintf("http://%s/apps/", server_address_));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->Start();
  delegate.RunUntilRedirect();

  const GURL new_url(
      base::StringPrintf("http://%s/apps/YouTube", server_address_));

  EXPECT_EQ(net::ERR_IO_PENDING, delegate.request_status());
  EXPECT_EQ(new_url, delegate.redirect_info().new_url);

  request->FollowDeferredRedirect(std::nullopt, std::nullopt);
  delegate.RunUntilComplete();

  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_OK, request->response_headers()->response_code());
  EXPECT_EQ(0, request->response_headers()->GetContentLength());
}

TEST_F(DialHttpServerTest, DdXmlWrongHttpMethod) {
  const GURL url(base::StringPrintf("http://%s/dd.xml", server_address_));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->set_method("POST");
  request->Start();
  delegate.RunUntilComplete();

  EXPECT_EQ(net::OK, delegate.request_status());
  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_NOT_FOUND, request->response_headers()->response_code());
}

TEST_F(DialHttpServerTest, CallbackNormalTest) {
  net::HttpServerResponseInfo response;
  response.SetBody("App Test", "text/plain; charset=\"utf-8\"");
  response.AddHeader("X-Test-Header", "Baz");
  EXPECT_CALL(*handler_,
              HandleRequest("GET", "Foo", "/bar", std::string(), _, _))
      .WillOnce(handler_->RunCallbackAction(std::move(response)));

  const GURL url(base::StringPrintf("http://%s/apps/Foo/bar", server_address_));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->Start();
  delegate.RunUntilComplete();

  EXPECT_EQ(net::OK, delegate.request_status());
  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_OK, request->response_headers()->response_code());
  EXPECT_TRUE(
      request->response_headers()->HasHeaderValue("x-test-header", "baz"));
  std::string mime_type;
  std::string charset;
  request->response_headers()->GetMimeTypeAndCharset(&mime_type, &charset);
  EXPECT_FALSE(mime_type.empty());
  EXPECT_FALSE(charset.empty());
  EXPECT_EQ("App Test", delegate.data_received());
}

TEST_F(DialHttpServerTest, RequestPathNormalization) {
  EXPECT_CALL(*handler_, HandleRequest("GET", "Foo", "/", std::string(), _, _))
      .WillOnce(handler_->RunCallbackAction(CreateEmptyResponse()))
      .WillOnce(handler_->RunCallbackAction(CreateEmptyResponse()));

  {
    const GURL url(base::StringPrintf("http://%s/apps/Foo", server_address_));
    net::TestDelegate delegate;
    auto request = context_->CreateRequest(
        url, net::DEFAULT_PRIORITY, &delegate, kNetworkTrafficAnnotation);
    request->Start();
    delegate.RunUntilComplete();

    EXPECT_EQ(net::OK, delegate.request_status());
    ASSERT_TRUE(request->response_headers());
    EXPECT_EQ(net::HTTP_OK, request->response_headers()->response_code());
  }
  {
    const GURL url(base::StringPrintf("http://%s/apps/Foo/", server_address_));
    net::TestDelegate delegate;
    auto request = context_->CreateRequest(
        url, net::DEFAULT_PRIORITY, &delegate, kNetworkTrafficAnnotation);
    request->Start();
    delegate.RunUntilComplete();

    EXPECT_EQ(net::OK, delegate.request_status());
    ASSERT_TRUE(request->response_headers());
    EXPECT_EQ(net::HTTP_OK, request->response_headers()->response_code());
  }
}

// Tests that if no handler is registered then the server responds with HTTP
// 404.
TEST_F(DialHttpServerTest, NoRegisteredHandler) {
  server_ = std::make_unique<DialHttpServer>(device_description_, nullptr);

  const GURL url(base::StringPrintf("http://%s/apps/Foo/",
                                    server_->GetLocalAddress().ToString()));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->Start();
  delegate.RunUntilComplete();

  EXPECT_EQ(net::OK, delegate.request_status());
  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_NOT_FOUND, request->response_headers()->response_code());
}

// Tests that a handler that returns std::nullopt results in an HTTP 404
// response.
TEST_F(DialHttpServerTest, HandlerReturnsNullopt) {
  EXPECT_CALL(*handler_, HandleRequest("GET", "Foo", "/", std::string(), _, _))
      .WillOnce(handler_->RunCallbackAction(std::nullopt));

  const GURL url(base::StringPrintf("http://%s/apps/Foo/", server_address_));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->Start();
  delegate.RunUntilComplete();

  EXPECT_EQ(net::OK, delegate.request_status());
  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_NOT_FOUND, request->response_headers()->response_code());
}

// Tests that a prefix other than "/apps/" returns 404 and does not invoke the
// handler.
TEST_F(DialHttpServerTest, InvalidPathInHandler) {
  EXPECT_CALL(*handler_, HandleRequest("GET", "Foo", "/", std::string(), _, _))
      .Times(0);

  const GURL url(
      base::StringPrintf("http://%s/other-prefix/YouTube/", server_address_));
  net::TestDelegate delegate;
  auto request = context_->CreateRequest(url, net::DEFAULT_PRIORITY, &delegate,
                                         kNetworkTrafficAnnotation);
  request->Start();
  delegate.RunUntilComplete();

  EXPECT_EQ(net::OK, delegate.request_status());
  ASSERT_TRUE(request->response_headers());
  EXPECT_EQ(net::HTTP_NOT_FOUND, request->response_headers()->response_code());
}

// Ensures the src address is not IFADDR_ANY or a loopback address.
TEST_F(DialHttpServerTest, GetLocalAddress) {
  const net::IPEndPoint addr = server_->GetLocalAddress();
  EXPECT_NE("0.0.0.0", addr.ToStringWithoutPort());
  EXPECT_NE("127.0.0.1", addr.ToStringWithoutPort());
}

}  // namespace in_app_dial
