// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/shared/starboard/media/mime_type.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace starboard {
namespace {

TEST(MimeTypeTest, EmptyString) {
  auto mime_type = MimeType::Create("");
  EXPECT_FALSE(mime_type);
}

// Valid mime type must have a type/subtype without space in between.
TEST(MimeTypeTest, InvalidMimeType) {
  {
    auto mime_type = MimeType::Create("video");
    EXPECT_FALSE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("video/");
    EXPECT_FALSE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("/mp4");
    EXPECT_FALSE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("video /mp4");
    EXPECT_FALSE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("video/ mp4");
    EXPECT_FALSE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("video / mp4");
    EXPECT_FALSE(mime_type);
  }

  {  // "codecs" parameter isn't the first parameter.
    auto mime_type =
        MimeType::Create("audio/mp4;param1=\" value1 \";codecs=\"abc, def\"");
    EXPECT_FALSE(mime_type);
  }

  {  // More than one "codecs" parameters.
    auto mime_type = MimeType::Create(
        "audio/mp4;codecs=\"abc, def\";"
        "param1=\" value1 \";codecs=\"abc, def\"");
    EXPECT_FALSE(mime_type);
  }
  {
    // Parameter name must not be empty.
    auto mime_type = MimeType::Create("video/mp4; =value");
    EXPECT_FALSE(mime_type);
  }
  {
    // Parameter value must not be empty.
    auto mime_type = MimeType::Create("video/mp4; name=");
    EXPECT_FALSE(mime_type);
  }
  {
    // No '|' allowed.
    auto mime_type = MimeType::Create("video/mp4; name=ab|c");
    EXPECT_FALSE(mime_type);
  }
}

TEST(MimeTypeTest, ValidContentTypeWithTypeAndSubtypeOnly) {
  {
    auto mime_type = MimeType::Create("video/mp4");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ("video", mime_type->type());
    EXPECT_EQ("mp4", mime_type->subtype());
  }

  {
    auto mime_type = MimeType::Create("audio/mp4");
    EXPECT_TRUE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("abc/xyz");
    EXPECT_TRUE(mime_type);
  }
}

TEST(MimeTypeTest, TypeNotAtBeginning) {
  auto mime_type = MimeType::Create("codecs=\"abc\"; audio/mp4");
  EXPECT_FALSE(mime_type);
}

TEST(MimeTypeTest, ValidContentTypeWithParams) {
  {
    auto mime_type = MimeType::Create("video/mp4; name=123");
    EXPECT_TRUE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("audio/mp4;codecs=\"abc\"");
    EXPECT_TRUE(mime_type);
  }

  {
    auto mime_type = MimeType::Create("  audio/mp4  ;  codecs   =  \"abc\"  ");
    EXPECT_TRUE(mime_type);
  }
}

TEST(MimeTypeTest, GetCodecs) {
  {
    auto mime_type = MimeType::Create("audio/mp4;codecs=\"abc\"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(1U, mime_type->GetCodecs().size());
    EXPECT_EQ("abc", mime_type->GetCodecs()[0]);
  }

  {
    auto mime_type = MimeType::Create("audio/mp4;codecs=\"abc, def\"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(2U, mime_type->GetCodecs().size());
    EXPECT_EQ("abc", mime_type->GetCodecs()[0]);
    EXPECT_EQ("def", mime_type->GetCodecs()[1]);
  }

  {
    auto mime_type =
        MimeType::Create("audio/mp4;codecs=\"abc, def\";param1=\" value1 \"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(2U, mime_type->GetCodecs().size());
  }
}

TEST(MimeTypeTest, ParamCount) {
  {
    auto mime_type = MimeType::Create("video/mp4");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(0, mime_type->GetParamCount());
  }

  {
    auto mime_type = MimeType::Create("video/mp4; width=1920");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(1, mime_type->GetParamCount());
  }

  {
    auto mime_type = MimeType::Create("video/mp4; width=1920; height=1080");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(2, mime_type->GetParamCount());
  }
}

TEST(MimeTypeTest, GetParamType) {
  {
    auto mime_type = MimeType::Create(
        "video/mp4; name0=123; name1=1.2; name2=xyz; name3=true; name4=false");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(MimeType::kParamTypeInteger, mime_type->GetParamType(0));
    EXPECT_EQ(MimeType::kParamTypeFloat, mime_type->GetParamType(1));
    EXPECT_EQ(MimeType::kParamTypeString, mime_type->GetParamType(2));
    EXPECT_EQ(MimeType::kParamTypeBoolean, mime_type->GetParamType(3));
    EXPECT_EQ(MimeType::kParamTypeBoolean, mime_type->GetParamType(4));
  }

  {
    auto mime_type =
        MimeType::Create("video/mp4; name0=\"123\"; name1=\"abc\"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(MimeType::kParamTypeString, mime_type->GetParamType(0));
    EXPECT_EQ(MimeType::kParamTypeString, mime_type->GetParamType(1));
  }

  {
    auto mime_type = MimeType::Create("video/mp4; name1=\" abc \"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(MimeType::kParamTypeString, mime_type->GetParamType(0));
  }
}

TEST(MimeTypeTest, GetParamName) {
  {
    auto mime_type =
        MimeType::Create("video/mp4; name0=123; name1=1.2; name2=xyz");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ("name0", mime_type->GetParamName(0));
    EXPECT_EQ("name1", mime_type->GetParamName(1));
    EXPECT_EQ("name2", mime_type->GetParamName(2));
  }
}

TEST(MimeTypeTest, GetParamIntValueWithIndex) {
  {
    auto mime_type = MimeType::Create("video/mp4; name=123");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(123, mime_type->GetParamIntValue(0));
  }

  {
    auto mime_type = MimeType::Create("video/mp4; width=1920; height=1080");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(1920, mime_type->GetParamIntValue(0));
    EXPECT_EQ(1080, mime_type->GetParamIntValue(1));
  }

  {
    auto mime_type = MimeType::Create("audio/mp4; channels=6");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(6, mime_type->GetParamIntValue(0));
  }
}

TEST(MimeTypeTest, GetParamFloatValueWithIndex) {
  auto mime_type = MimeType::Create("video/mp4; name0=123; name1=123.4");
  ASSERT_TRUE(mime_type);
  EXPECT_FLOAT_EQ(123.f, mime_type->GetParamFloatValue(0));
  EXPECT_FLOAT_EQ(123.4f, mime_type->GetParamFloatValue(1));
}

TEST(MimeTypeTest, GetParamStringValueWithIndex) {
  {
    auto mime_type =
        MimeType::Create("video/mp4; name0=123; name1=abc; name2=\"xyz\"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ("123", mime_type->GetParamStringValue(0));
    EXPECT_EQ("abc", mime_type->GetParamStringValue(1));
    EXPECT_EQ("xyz", mime_type->GetParamStringValue(2));
  }

  {
    auto mime_type = MimeType::Create("video/mp4; name=\" xyz  \"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(" xyz  ", mime_type->GetParamStringValue(0));
  }
}

TEST(MimeTypeTest, GetParamBoolValueWithIndex) {
  auto mime_type = MimeType::Create("video/mp4; name0=true; name1=false");
  ASSERT_TRUE(mime_type);
  EXPECT_TRUE(mime_type->GetParamBoolValue(0));
  EXPECT_FALSE(mime_type->GetParamBoolValue(1));
}

TEST(MimeTypeTest, GetParamValueInInvalidType) {
  auto mime_type = MimeType::Create("video/mp4; name0=abc; name1=123.4");
  ASSERT_TRUE(mime_type);
  EXPECT_FLOAT_EQ(0, mime_type->GetParamIntValue(0));
  EXPECT_FLOAT_EQ(0.f, mime_type->GetParamFloatValue(0));
  EXPECT_FLOAT_EQ(0, mime_type->GetParamIntValue(1));
}

TEST(MimeTypeTest, GetParamIntValueWithName) {
  {
    auto mime_type = MimeType::Create("video/mp4; name=123");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(123, mime_type->GetParamIntValue("name", 0));
    EXPECT_EQ(6, mime_type->GetParamIntValue(MimeType::Param::kChannels, 6));
  }

  {
    auto mime_type = MimeType::Create("video/mp4; width=1920; height=1080");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(1920, mime_type->GetParamIntValue(MimeType::Param::kWidth, 0));
    EXPECT_EQ(1080, mime_type->GetParamIntValue(MimeType::Param::kHeight, 0));
  }

  {
    auto mime_type = MimeType::Create("audio/mp4; channels=6");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(6, mime_type->GetParamIntValue(MimeType::Param::kChannels, 0));
  }
}

TEST(MimeTypeTest, GetParamFloatValueWithName) {
  {
    auto mime_type = MimeType::Create("video/mp4; name0=123; name1=123.4");
    ASSERT_TRUE(mime_type);
    EXPECT_FLOAT_EQ(123.f, mime_type->GetParamFloatValue("name0", 0.f));
    EXPECT_FLOAT_EQ(123.4f, mime_type->GetParamFloatValue("name1", 0.f));
    EXPECT_FLOAT_EQ(59.96f, mime_type->GetParamFloatValue(
                                MimeType::Param::kFramerate, 59.96f));
  }
}

TEST(MimeTypeTest, GetParamStringValueWithName) {
  {
    auto mime_type =
        MimeType::Create("video/mp4; name0=123; name1=abc; name2=\"xyz\"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ("123", mime_type->GetParamStringValue("name0", ""));
    EXPECT_EQ("abc", mime_type->GetParamStringValue("name1", ""));
    EXPECT_EQ("xyz", mime_type->GetParamStringValue("name2", ""));
    EXPECT_EQ("h263",
              mime_type->GetParamStringValue(MimeType::Param::kCodecs, "h263"));
  }

  {
    auto mime_type = MimeType::Create("video/mp4; name=\" xyz  \"");
    ASSERT_TRUE(mime_type);
    EXPECT_EQ(" xyz  ", mime_type->GetParamStringValue("name", ""));
  }
}

TEST(MimeTypeTest, GetParamBooleanValueWithName) {
  auto mime_type =
      MimeType::Create("video/mp4; name0=true; name1=false; name2=trues");
  ASSERT_TRUE(mime_type);
  EXPECT_TRUE(mime_type->GetParamBoolValue("name0", false));
  EXPECT_FALSE(mime_type->GetParamBoolValue("name1", true));
  // Default value
  EXPECT_FALSE(mime_type->GetParamBoolValue("name2", false));
}

TEST(MimeTypeTest, ReadParamOfDifferentTypes) {
  // Ensure that the parameter type is enforced correctly.
  auto mime_type =
      MimeType::Create("video/mp4; string=value; int=1; float=1.0; bool=true");
  ASSERT_TRUE(mime_type);
  // Read params as their original types.
  EXPECT_EQ(mime_type->GetParamStringValue("string", ""), "value");
  EXPECT_EQ(mime_type->GetParamIntValue("int", 0), 1);
  EXPECT_EQ(mime_type->GetParamFloatValue("float", 0.0), 1.0);
  EXPECT_EQ(mime_type->GetParamBoolValue("bool", false), true);
  // All param types can be read as strings.
  EXPECT_EQ(mime_type->GetParamStringValue("int", ""), "1");
  EXPECT_EQ(mime_type->GetParamStringValue("float", ""), "1.0");
  EXPECT_EQ(mime_type->GetParamStringValue("bool", ""), "true");
  // Int can also be read as float.
  EXPECT_EQ(mime_type->GetParamFloatValue("int", 0.0), 1.0);

  // Test failing validation for ints,
  EXPECT_EQ(mime_type->GetParamIntValue("string", 0), 0);
  EXPECT_EQ(mime_type->GetParamIntValue("float", 0), 0);
  EXPECT_EQ(mime_type->GetParamIntValue("bool", 0), 0);

  // floats,
  EXPECT_EQ(mime_type->GetParamFloatValue("string", 0.0), 0.0);
  EXPECT_EQ(mime_type->GetParamFloatValue("bool", 0.0), 0.0);

  // and bools.
  EXPECT_FALSE(mime_type->GetParamBoolValue("string", false));
  EXPECT_FALSE(mime_type->GetParamBoolValue("int", false));
  EXPECT_FALSE(mime_type->GetParamBoolValue("float", false));
}

TEST(MimeTypeTest, ParseInvalidParamString) {
  EXPECT_FALSE(MimeType::ParseParamString("", nullptr));
  EXPECT_FALSE(MimeType::ParseParamString("invalid", nullptr));
  EXPECT_FALSE(MimeType::ParseParamString("val=0;", nullptr));
  EXPECT_FALSE(MimeType::ParseParamString("val=a|b", nullptr));
  EXPECT_FALSE(MimeType::ParseParamString("val=", nullptr));
}

TEST(MimeTypeTest, ParseValidParamString) {
  MimeType::Param result;

  EXPECT_TRUE(MimeType::ParseParamString("val=0", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeInteger);
  EXPECT_EQ(result.int_value, 0);

  EXPECT_TRUE(MimeType::ParseParamString("val=1", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeInteger);
  EXPECT_EQ(result.int_value, 1);

  EXPECT_TRUE(MimeType::ParseParamString("val=-1", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeInteger);
  EXPECT_EQ(result.int_value, -1);

  EXPECT_TRUE(MimeType::ParseParamString("val=0.0", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeFloat);
  EXPECT_EQ(result.float_value, 0.0f);

  EXPECT_TRUE(MimeType::ParseParamString("val=1.0", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeFloat);
  EXPECT_EQ(result.float_value, 1.0f);

  EXPECT_TRUE(MimeType::ParseParamString("val=-1.0", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeFloat);
  EXPECT_EQ(result.float_value, -1.0f);

  EXPECT_TRUE(MimeType::ParseParamString("val=true", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeBoolean);
  EXPECT_EQ(result.bool_value, true);

  EXPECT_TRUE(MimeType::ParseParamString("val=false", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeBoolean);
  EXPECT_EQ(result.bool_value, false);

  EXPECT_TRUE(MimeType::ParseParamString("val=\"\"", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeString);
  EXPECT_EQ(result.string_value, "");

  EXPECT_TRUE(MimeType::ParseParamString("val=\"abc\"", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeString);
  EXPECT_EQ(result.string_value, "abc");

  EXPECT_TRUE(MimeType::ParseParamString("val=abc", &result));
  EXPECT_EQ(result.name, "val");
  EXPECT_EQ(result.type, MimeType::kParamTypeString);
  EXPECT_EQ(result.string_value, "abc");
}

TEST(MimeTypeTest, ValidateParamsWithPatterns) {
  auto mime_type = MimeType::Create("video/mp4; string=yes");
  ASSERT_TRUE(mime_type);
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "yes"));
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "yes|no"));
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "no|yes|no"));
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "no|no|yes"));
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "noyes|yes"));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "no"));
}

TEST(MimeTypeTest, ValidateParamsWithShortPatterns) {
  auto mime_type = MimeType::Create("video/mp4; string=y");
  ASSERT_TRUE(mime_type);
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "y"));
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "y|n"));
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", "n|y"));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "n"));
}

TEST(MimeTypeTest, ValidateParamsWithPartialMatches) {
  auto mime_type = MimeType::Create("video/mp4; string=yes");
  ASSERT_TRUE(mime_type);
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "yesno|no"));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "noyes|no"));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "no|yesno"));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "no|noyes"));
}

TEST(MimeTypeTest, ValidateMissingParam) {
  auto mime_type = MimeType::Create("video/mp4");
  ASSERT_TRUE(mime_type);
  EXPECT_TRUE(mime_type->ValidateStringParameter("string"));
  EXPECT_EQ(mime_type->GetParamStringValue("string", "default"), "default");
}

TEST(MimeTypeTest, ValidateParamsWithEmptyishPattern) {
  auto mime_type = MimeType::Create("video/mp4; string=yes");
  ASSERT_TRUE(mime_type);
  EXPECT_TRUE(mime_type->ValidateStringParameter("string", ""));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "|"));
  EXPECT_FALSE(mime_type->ValidateStringParameter("string", "||"));
}

TEST(MimeTypeTest, ValidateParamWithInvalidMimeType) {
  auto mime_type = MimeType::Create("video/mp4; string=");
  ASSERT_FALSE(mime_type);
}

TEST(MimeTypeTest, ParamConstants) {
  EXPECT_STREQ(MimeType::Param::kCodecs, "codecs");

  EXPECT_STREQ(MimeType::Param::kWidth, "width");
  EXPECT_STREQ(MimeType::Param::kHeight, "height");
  EXPECT_STREQ(MimeType::Param::kFramerate, "framerate");
  EXPECT_STREQ(MimeType::Param::kBitrate, "bitrate");
  EXPECT_STREQ(MimeType::Param::kEotf, "eotf");
  EXPECT_STREQ(MimeType::Param::kChannels, "channels");
  EXPECT_STREQ(MimeType::Param::kCryptoblockformat, "cryptoblockformat");
  EXPECT_STREQ(MimeType::Param::kDecodeToTexture, "decode-to-texture");
  EXPECT_STREQ(MimeType::Param::kExperimental, "experimental");
  EXPECT_STREQ(MimeType::Param::kTunnelMode, "tunnelmode");

  EXPECT_STREQ(MimeType::Param::kEnableFlushDuringSeek,
               "enableflushduringseek");
  EXPECT_STREQ(MimeType::Param::kEnableResetAudioDecoder,
               "enableresetaudiodecoder");
}

}  // namespace

}  // namespace starboard
