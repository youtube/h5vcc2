// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/gles.h"

#include <stdio.h>

#include <string>

#include "starboard/testing/fake_graphics_context_provider.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace nplb {
namespace {

// Verifies that we are able to query for the Starboard OpenGL ES interface, and
// that the interface is correctly populated with function pointers that cover
// the OpenGL ES 2.0 interface.

TEST(SbGlesInterfaceTest, HasValidGlesInterface) {
  const SbGlesInterface* const gles_interface = SbGetGlesInterface();

  ASSERT_NE(nullptr, gles_interface);

  EXPECT_NE(nullptr, gles_interface->glActiveTexture);
  EXPECT_NE(nullptr, gles_interface->glAttachShader);
  EXPECT_NE(nullptr, gles_interface->glBindAttribLocation);
  EXPECT_NE(nullptr, gles_interface->glBindBuffer);
  EXPECT_NE(nullptr, gles_interface->glBindFramebuffer);
  EXPECT_NE(nullptr, gles_interface->glBindRenderbuffer);
  EXPECT_NE(nullptr, gles_interface->glBindTexture);
  EXPECT_NE(nullptr, gles_interface->glBlendColor);
  EXPECT_NE(nullptr, gles_interface->glBlendEquation);
  EXPECT_NE(nullptr, gles_interface->glBlendEquationSeparate);
  EXPECT_NE(nullptr, gles_interface->glBlendFunc);
  EXPECT_NE(nullptr, gles_interface->glBlendFuncSeparate);
  EXPECT_NE(nullptr, gles_interface->glBufferData);
  EXPECT_NE(nullptr, gles_interface->glBufferSubData);
  EXPECT_NE(nullptr, gles_interface->glCheckFramebufferStatus);
  EXPECT_NE(nullptr, gles_interface->glClear);
  EXPECT_NE(nullptr, gles_interface->glClearColor);
  EXPECT_NE(nullptr, gles_interface->glClearDepthf);
  EXPECT_NE(nullptr, gles_interface->glClearStencil);
  EXPECT_NE(nullptr, gles_interface->glColorMask);
  EXPECT_NE(nullptr, gles_interface->glCompileShader);
  EXPECT_NE(nullptr, gles_interface->glCompressedTexImage2D);
  EXPECT_NE(nullptr, gles_interface->glCompressedTexSubImage2D);
  EXPECT_NE(nullptr, gles_interface->glCopyTexImage2D);
  EXPECT_NE(nullptr, gles_interface->glCopyTexSubImage2D);
  EXPECT_NE(nullptr, gles_interface->glCreateProgram);
  EXPECT_NE(nullptr, gles_interface->glCreateShader);
  EXPECT_NE(nullptr, gles_interface->glCullFace);
  EXPECT_NE(nullptr, gles_interface->glDeleteBuffers);
  EXPECT_NE(nullptr, gles_interface->glDeleteFramebuffers);
  EXPECT_NE(nullptr, gles_interface->glDeleteProgram);
  EXPECT_NE(nullptr, gles_interface->glDeleteRenderbuffers);
  EXPECT_NE(nullptr, gles_interface->glDeleteShader);
  EXPECT_NE(nullptr, gles_interface->glDeleteTextures);
  EXPECT_NE(nullptr, gles_interface->glDepthFunc);
  EXPECT_NE(nullptr, gles_interface->glDepthMask);
  EXPECT_NE(nullptr, gles_interface->glDepthRangef);
  EXPECT_NE(nullptr, gles_interface->glDetachShader);
  EXPECT_NE(nullptr, gles_interface->glDisable);
  EXPECT_NE(nullptr, gles_interface->glDisableVertexAttribArray);
  EXPECT_NE(nullptr, gles_interface->glDrawArrays);
  EXPECT_NE(nullptr, gles_interface->glDrawElements);
  EXPECT_NE(nullptr, gles_interface->glEnable);
  EXPECT_NE(nullptr, gles_interface->glEnableVertexAttribArray);
  EXPECT_NE(nullptr, gles_interface->glFinish);
  EXPECT_NE(nullptr, gles_interface->glFlush);
  EXPECT_NE(nullptr, gles_interface->glFramebufferRenderbuffer);
  EXPECT_NE(nullptr, gles_interface->glFramebufferTexture2D);
  EXPECT_NE(nullptr, gles_interface->glFrontFace);
  EXPECT_NE(nullptr, gles_interface->glGenBuffers);
  EXPECT_NE(nullptr, gles_interface->glGenerateMipmap);
  EXPECT_NE(nullptr, gles_interface->glGenFramebuffers);
  EXPECT_NE(nullptr, gles_interface->glGenRenderbuffers);
  EXPECT_NE(nullptr, gles_interface->glGenTextures);
  EXPECT_NE(nullptr, gles_interface->glGetActiveAttrib);
  EXPECT_NE(nullptr, gles_interface->glGetActiveUniform);
  EXPECT_NE(nullptr, gles_interface->glGetAttachedShaders);
  EXPECT_NE(nullptr, gles_interface->glGetAttribLocation);
  EXPECT_NE(nullptr, gles_interface->glGetBooleanv);
  EXPECT_NE(nullptr, gles_interface->glGetBufferParameteriv);
  EXPECT_NE(nullptr, gles_interface->glGetError);
  EXPECT_NE(nullptr, gles_interface->glGetFloatv);
  EXPECT_NE(nullptr, gles_interface->glGetFramebufferAttachmentParameteriv);
  EXPECT_NE(nullptr, gles_interface->glGetIntegerv);
  EXPECT_NE(nullptr, gles_interface->glGetProgramiv);
  EXPECT_NE(nullptr, gles_interface->glGetProgramInfoLog);
  EXPECT_NE(nullptr, gles_interface->glGetRenderbufferParameteriv);
  EXPECT_NE(nullptr, gles_interface->glGetShaderiv);
  EXPECT_NE(nullptr, gles_interface->glGetShaderInfoLog);
  EXPECT_NE(nullptr, gles_interface->glGetShaderPrecisionFormat);
  EXPECT_NE(nullptr, gles_interface->glGetShaderSource);
  EXPECT_NE(nullptr, gles_interface->glGetString);
  EXPECT_NE(nullptr, gles_interface->glGetTexParameterfv);
  EXPECT_NE(nullptr, gles_interface->glGetTexParameteriv);
  EXPECT_NE(nullptr, gles_interface->glGetUniformfv);
  EXPECT_NE(nullptr, gles_interface->glGetUniformiv);
  EXPECT_NE(nullptr, gles_interface->glGetUniformLocation);
  EXPECT_NE(nullptr, gles_interface->glGetVertexAttribfv);
  EXPECT_NE(nullptr, gles_interface->glGetVertexAttribiv);
  EXPECT_NE(nullptr, gles_interface->glGetVertexAttribPointerv);
  EXPECT_NE(nullptr, gles_interface->glHint);
  EXPECT_NE(nullptr, gles_interface->glIsBuffer);
  EXPECT_NE(nullptr, gles_interface->glIsEnabled);
  EXPECT_NE(nullptr, gles_interface->glIsFramebuffer);
  EXPECT_NE(nullptr, gles_interface->glIsProgram);
  EXPECT_NE(nullptr, gles_interface->glIsRenderbuffer);
  EXPECT_NE(nullptr, gles_interface->glIsShader);
  EXPECT_NE(nullptr, gles_interface->glIsTexture);
  EXPECT_NE(nullptr, gles_interface->glLineWidth);
  EXPECT_NE(nullptr, gles_interface->glLinkProgram);
  EXPECT_NE(nullptr, gles_interface->glPixelStorei);
  EXPECT_NE(nullptr, gles_interface->glPolygonOffset);
  EXPECT_NE(nullptr, gles_interface->glReadPixels);
  EXPECT_NE(nullptr, gles_interface->glReleaseShaderCompiler);
  EXPECT_NE(nullptr, gles_interface->glRenderbufferStorage);
  EXPECT_NE(nullptr, gles_interface->glSampleCoverage);
  EXPECT_NE(nullptr, gles_interface->glScissor);
  EXPECT_NE(nullptr, gles_interface->glShaderBinary);
  EXPECT_NE(nullptr, gles_interface->glShaderSource);
  EXPECT_NE(nullptr, gles_interface->glStencilFunc);
  EXPECT_NE(nullptr, gles_interface->glStencilFuncSeparate);
  EXPECT_NE(nullptr, gles_interface->glStencilMask);
  EXPECT_NE(nullptr, gles_interface->glStencilMaskSeparate);
  EXPECT_NE(nullptr, gles_interface->glStencilOp);
  EXPECT_NE(nullptr, gles_interface->glStencilOpSeparate);
  EXPECT_NE(nullptr, gles_interface->glTexImage2D);
  EXPECT_NE(nullptr, gles_interface->glTexParameterf);
  EXPECT_NE(nullptr, gles_interface->glTexParameterfv);
  EXPECT_NE(nullptr, gles_interface->glTexParameteri);
  EXPECT_NE(nullptr, gles_interface->glTexParameteriv);
  EXPECT_NE(nullptr, gles_interface->glTexSubImage2D);
  EXPECT_NE(nullptr, gles_interface->glUniform1f);
  EXPECT_NE(nullptr, gles_interface->glUniform1fv);
  EXPECT_NE(nullptr, gles_interface->glUniform1i);
  EXPECT_NE(nullptr, gles_interface->glUniform1iv);
  EXPECT_NE(nullptr, gles_interface->glUniform2f);
  EXPECT_NE(nullptr, gles_interface->glUniform2fv);
  EXPECT_NE(nullptr, gles_interface->glUniform2i);
  EXPECT_NE(nullptr, gles_interface->glUniform2iv);
  EXPECT_NE(nullptr, gles_interface->glUniform3f);
  EXPECT_NE(nullptr, gles_interface->glUniform3fv);
  EXPECT_NE(nullptr, gles_interface->glUniform3i);
  EXPECT_NE(nullptr, gles_interface->glUniform3iv);
  EXPECT_NE(nullptr, gles_interface->glUniform4f);
  EXPECT_NE(nullptr, gles_interface->glUniform4fv);
  EXPECT_NE(nullptr, gles_interface->glUniform4i);
  EXPECT_NE(nullptr, gles_interface->glUniform4iv);
  EXPECT_NE(nullptr, gles_interface->glUniformMatrix2fv);
  EXPECT_NE(nullptr, gles_interface->glUniformMatrix3fv);
  EXPECT_NE(nullptr, gles_interface->glUniformMatrix4fv);
  EXPECT_NE(nullptr, gles_interface->glUseProgram);
  EXPECT_NE(nullptr, gles_interface->glValidateProgram);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib1f);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib1fv);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib2f);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib2fv);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib3f);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib3fv);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib4f);
  EXPECT_NE(nullptr, gles_interface->glVertexAttrib4fv);
  EXPECT_NE(nullptr, gles_interface->glVertexAttribPointer);
  EXPECT_NE(nullptr, gles_interface->glViewport);
}

// Verifies that the target device supports OpenGLES 3.2 or higher graphics API.
TEST(SbGlesTest, SupportsOpenGles32OrHigher) {
  const SbGlesInterface* const gles_interface = SbGetGlesInterface();
  ASSERT_NE(nullptr, gles_interface);

  starboard::FakeGraphicsContextProvider fake_graphics_context_provider;
  std::string version_string;

  fake_graphics_context_provider.RunOnGlesContextThread([&]() {
    const char* str = reinterpret_cast<const char*>(
        gles_interface->glGetString(SB_GL_VERSION));
    if (str) {
      version_string = str;
    }
  });

  ASSERT_FALSE(version_string.empty());

  int major = 0;
  int minor = 0;
  int parsed =
      sscanf(version_string.c_str(), "OpenGL ES %d.%d", &major, &minor);
  if (parsed != 2) {
    parsed = sscanf(version_string.c_str(), "%d.%d", &major, &minor);
  }
  ASSERT_EQ(2, parsed) << "Failed to parse OpenGL ES version string: "
                       << version_string;

  // The target device MUST support OpenGLES 3.2 or higher graphics API.
  EXPECT_TRUE(major > 3 || (major == 3 && minor >= 2))
      << "Expected OpenGLES 3.2 or higher, but found OpenGLES " << major << "."
      << minor << " (version string: " << version_string << ")";
}

}  // namespace
}  // namespace nplb
