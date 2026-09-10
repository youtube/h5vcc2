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

// Many systems have only a few locales defined. When a test requires a
// specific locale that is not available, the test will be skipped.

#include <locale.h>
#include <string.h>

#include <algorithm>
#include <cerrno>
#include <clocale>
#include <cstring>
#include <string>
#include <vector>

#include "build/build_config.h"
#include "starboard/nplb/posix_compliance/posix_compliance_icu.h"
#include "starboard/nplb/posix_compliance/posix_locale_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

constexpr char kDefaultLocale[] = "C";
constexpr char kPosixLocale[] = "POSIX";

namespace starboard {
namespace nplb {

// RAII class to save and restore locale.
class ScopedLocale {
 public:
  ScopedLocale() {
    starboard::nplb::InitializePosixIcuOnce();
    original_locale_ = setlocale(LC_ALL, NULL);
    // It's possible for setlocale to return NULL if the locale is invalid,
    // but we assume the initial state is valid. We duplicate the string
    // because the pointer might be invalidated by subsequent setlocale calls.
    if (original_locale_) {
      original_locale_ = strdup(original_locale_);
    }
  }

  ~ScopedLocale() {
    if (original_locale_) {
      setlocale(LC_ALL, original_locale_);
      free(original_locale_);
    }
  }

 private:
  char* original_locale_;
};

TEST(PosixLocaleSetTest, SetLocaleC) {
  ScopedLocale scoped_locale;
  const char* result = setlocale(LC_ALL, kDefaultLocale);
  ASSERT_NE(nullptr, result);
  EXPECT_STREQ(kDefaultLocale, result);
}

TEST(PosixLocaleSetTest, SetLocalePosix) {
  ScopedLocale scoped_locale;
  const char* kTestLocale = LocaleFinder::FindSupported({kPosixLocale});
  if (!kTestLocale) {
    GTEST_SKIP() << "The \"POSIX\" locale is not supported: "
                 << strerror(errno);
  }
  const char* result = setlocale(LC_ALL, kTestLocale);
  if (!result) {
    GTEST_SKIP() << "The \"" << kTestLocale
                 << "\" locale is not supported: " << strerror(errno);
  }
  // kPosixLocale is an alias for kDefaultLocale. Some systems may return
  // kDefaultLocale when kPosixLocale is requested.
  EXPECT_TRUE(strcmp(result, kTestLocale) == 0 ||
              strcmp(result, kDefaultLocale) == 0);
}

TEST(PosixLocaleSetTest, QueryLocale) {
  ScopedLocale scoped_locale;
  setlocale(LC_ALL, kDefaultLocale);
  const char* result = setlocale(LC_ALL, NULL);
  ASSERT_NE(nullptr, result);
  EXPECT_STREQ(kDefaultLocale, result);
}

TEST(PosixLocaleSetTest, InvalidLocale) {
  ScopedLocale scoped_locale;
  const char* result = setlocale(LC_ALL, "invalid-locale-does-not-exist");
  EXPECT_EQ(nullptr, result);
}

TEST(PosixLocaleSetTest, LocaleConvC) {
  ScopedLocale scoped_locale;
  setlocale(LC_ALL, kDefaultLocale);
  struct lconv* conv = localeconv();
  ASSERT_NE(nullptr, conv);
  EXPECT_STREQ(".", conv->decimal_point);
  EXPECT_STREQ("", conv->thousands_sep);
  EXPECT_STREQ("", conv->grouping);
  EXPECT_TRUE(strcmp(conv->int_curr_symbol, "") == 0 ||
              strcmp(conv->int_curr_symbol, kDefaultLocale) == 0);
  EXPECT_TRUE(strcmp(conv->currency_symbol, "") == 0 ||
              strcmp(conv->currency_symbol, kDefaultLocale) == 0);
  EXPECT_STREQ("", conv->mon_decimal_point);
  EXPECT_STREQ("", conv->mon_thousands_sep);
  EXPECT_STREQ("", conv->mon_grouping);
  EXPECT_STREQ("", conv->positive_sign);
  EXPECT_STREQ("", conv->negative_sign);
}

// newlocale, uselocale, and freelocale are part of POSIX.1-2008 and may not be
// available on all platforms. Check for _XOPEN_SOURCE >= 700 or similar macros.
// Starboard seems to aim for POSIX.1-2008 compliance, so these should be
// available.
TEST(PosixLocaleSetTest, NewUseFreeLocale) {
  ScopedLocale scoped_locale;

  // Set a known global locale.
  ASSERT_NE(nullptr, setlocale(LC_ALL, kDefaultLocale));
  ASSERT_NE(nullptr, localeconv());
  const char* initial_decimal_point = localeconv()->decimal_point;

  locale_t c_locale = newlocale(LC_ALL_MASK, kDefaultLocale, (locale_t)0);
  ASSERT_NE((locale_t)0, c_locale);

  // Try to use a different locale if available.
  // en_US.UTF-8 is common. If not, this part of the test will be skipped.
  const char* kTestLocale = LocaleFinder::FindSupported({"en_US.UTF-8"});
  if (!kTestLocale) {
    GTEST_SKIP() << "The \"en_US.UTF-8\" locale is not supported: "
                 << strerror(errno);
  }
  locale_t us_locale = newlocale(LC_ALL_MASK, kTestLocale, (locale_t)0);
  if (us_locale != (locale_t)0) {
    locale_t original_thread_locale = uselocale(us_locale);

    // Check that the thread's locale has changed.
    // In en_US, decimal point is '.'
    ASSERT_NE(nullptr, localeconv());
    EXPECT_STREQ(".", localeconv()->decimal_point);

    // The global locale should be unchanged.
    // To check this, we need to temporarily switch back the thread locale.
    uselocale(original_thread_locale);
    EXPECT_STREQ(initial_decimal_point, localeconv()->decimal_point);
    // Switch back to us_locale for further checks.
    uselocale(us_locale);

    // Restore original thread locale.
    uselocale(original_thread_locale);
    freelocale(us_locale);
  } else {
    // If en_US.UTF-8 is not available, we can't test switching.
    // Just test uselocale with the C locale we created.
    locale_t original_thread_locale = uselocale(c_locale);
    ASSERT_NE(nullptr, localeconv());
    EXPECT_STREQ(".", localeconv()->decimal_point);
    uselocale(original_thread_locale);
  }

  freelocale(c_locale);
}

TEST(PosixLocaleSetTest, NewLocaleInvalid) {
  locale_t loc = newlocale(LC_ALL_MASK, "invalid-locale-name", (locale_t)0);
  EXPECT_EQ((locale_t)0, loc);
}

TEST(PosixLocaleSetTest, UseLocaleGlobal) {
  ScopedLocale scoped_locale;
  setlocale(LC_ALL, kDefaultLocale);
  locale_t original_thread_locale = uselocale(LC_GLOBAL_LOCALE);
  EXPECT_EQ(LC_GLOBAL_LOCALE, original_thread_locale);
  ASSERT_NE(nullptr, localeconv());
  EXPECT_STREQ(".", localeconv()->decimal_point);
  uselocale(original_thread_locale);
}

struct LocaleCategoryParam {
  int category;
  const char* name;
};

class PosixLocaleSetCategoryTest
    : public ::testing::TestWithParam<LocaleCategoryParam> {};

TEST_P(PosixLocaleSetCategoryTest, SetAllThenQueryCategory) {
  ScopedLocale scoped_locale;

  // Attempt to set a non-C locale first to ensure we can see a change.
  // en_US.UTF-8 is commonly available.
  const char* kTestLocale = LocaleFinder::FindSupported({"en_US.UTF-8"});
  if (!kTestLocale) {
    GTEST_SKIP() << "The \"en_US.UTF-8\" locale is not supported: "
                 << strerror(errno);
  }
  const char* initial_set = setlocale(LC_ALL, kTestLocale);
  if (!initial_set) {
    GTEST_SKIP() << "Test skipped: en_US.UTF-8 locale not available.";
  }

  int category = GetParam().category;
  char* category_locale_1 = strdup(setlocale(category, NULL));
  ASSERT_NE(nullptr, category_locale_1);
  EXPECT_STREQ(kTestLocale, category_locale_1);

  // Now, set LC_ALL to kDefaultLocale and verify the specific category has
  // changed.
  const char* result = setlocale(LC_ALL, kDefaultLocale);
  ASSERT_NE(nullptr, result);

  char* category_locale_2 = strdup(setlocale(category, NULL));
  ASSERT_NE(nullptr, category_locale_2);
  EXPECT_STREQ(kDefaultLocale, category_locale_2);

  // Ensure the locale actually changed.
  EXPECT_STRNE(category_locale_1, category_locale_2);
  free(category_locale_1);
  free(category_locale_2);
}

const LocaleCategoryParam kLocaleCategories[] = {
    {LC_CTYPE, "LcCtype"},       {LC_NUMERIC, "LcNumeric"},
    {LC_TIME, "LcTime"},         {LC_COLLATE, "LcCollate"},
    {LC_MONETARY, "LcMonetary"}, {LC_MESSAGES, "LcMessages"},
};

INSTANTIATE_TEST_SUITE_P(
    PosixLocaleSetTests,
    PosixLocaleSetCategoryTest,
    ::testing::ValuesIn(kLocaleCategories),
    [](const ::testing::TestParamInfo<LocaleCategoryParam>& info) {
      return info.param.name;
    });

struct LocaleFormatParam {
  const char* locale_string;
};

void PrintTo(const LocaleFormatParam& param, ::std::ostream* os) {
  *os << "LocaleFormatParam(\"" << param.locale_string << "\")";
}

class PosixLocaleSetFormatTest
    : public ::testing::TestWithParam<LocaleFormatParam> {};

class PosixLocaleSetAlternativeFormatTest
    : public ::testing::TestWithParam<LocaleFormatParam> {};

TEST_P(PosixLocaleSetFormatTest, HandlesLocaleString) {
  ScopedLocale scoped_locale;
  const auto& param = GetParam();

  const char* result = setlocale(LC_ALL, param.locale_string);
  ASSERT_NE(nullptr, result)
      << "Expected setlocale to succeed for: " << param.locale_string;

  const char* kTestLocale = LocaleFinder::FindSupported({kPosixLocale});
  if (kTestLocale && strcmp(param.locale_string, kTestLocale) == 0) {
    EXPECT_TRUE(strcmp(result, kTestLocale) == 0 ||
                strcmp(result, kDefaultLocale) == 0);
  } else if (strlen(param.locale_string) > 0) {
    EXPECT_EQ(static_cast<std::string::size_type>(0),
              std::string(result).find(param.locale_string))
        << "Returned locale " << result << " does not match requested "
        << param.locale_string;
  }
}

TEST_P(PosixLocaleSetAlternativeFormatTest, HandlesLocaleStringWithCodeSet) {
  ScopedLocale scoped_locale;
  const auto& param = GetParam();

  std::string locale_string_with_codeset = param.locale_string;

  // if the locale comes with an @ modifier like "sr_RS@latin", the codeset must
  // be added before the modifier, not after.
  if (locale_string_with_codeset.find('@') != std::string::npos) {
    locale_string_with_codeset.insert(locale_string_with_codeset.find('@'),
                                      ".UTF-8");
  } else {
    locale_string_with_codeset.append(".UTF-8");
  }

  const char* result = setlocale(LC_ALL, locale_string_with_codeset.c_str());
  ASSERT_NE(nullptr, result)
      << "Expected setlocale to succeed for: " << locale_string_with_codeset;
  EXPECT_STREQ(locale_string_with_codeset.c_str(), result);
}

#if BUILDFLAG(IS_COBALT_HERMETIC_BUILD)
TEST_P(PosixLocaleSetAlternativeFormatTest, HandlesLocaleStringInBcp47Format) {
  ScopedLocale scoped_locale;
  const auto& param = GetParam();

  std::string bcp47_locale_string = param.locale_string;
  std::replace(bcp47_locale_string.begin(), bcp47_locale_string.end(), '_',
               '-');

  const char* result = setlocale(LC_ALL, bcp47_locale_string.c_str());
  ASSERT_NE(nullptr, result)
      << "Expected setlocale to succeed for: " << bcp47_locale_string;

  // The returned string should be consistent with the requested locale,
  // likely in the POSIX format.
  EXPECT_EQ(static_cast<std::string::size_type>(0),
            std::string(result).find(param.locale_string))
      << "Returned locale " << result << " does not match requested "
      << param.locale_string << " (from BCP-47 " << bcp47_locale_string << ")";
}
#endif  // BUILDFLAG(IS_COBALT_HERMETIC_BUILD)

TEST(PosixLocaleSetTest, InvalidLocaleFormat) {
  ScopedLocale scoped_locale;
  const char* result = setlocale(LC_ALL, "invalid-locale");
  EXPECT_EQ(nullptr, result);
}

const LocaleFormatParam kSpecialLocaleFormats[] = {
    {kDefaultLocale},
    {kPosixLocale},
    {""},  // An empty string should query the current locale.
};

const LocaleFormatParam kLocaleFormats[] = {
    // Comprehensive language list
    {"af_ZA"},   // Afrikaans
    {"sq_AL"},   // Albanian
    {"am_ET"},   // Amharic
    {"ar_SA"},   // Arabic
    {"hy_AM"},   // Armenian
    {"as_IN"},   // Assamese
    {"az_AZ"},   // Azerbaijani
    {"bn_BD"},   // Bangla
    {"eu_ES"},   // Basque
    {"be_BY"},   // Belarusian
    {"bs_BA"},   // Bosnian
    {"bg_BG"},   // Bulgarian
    {"ca_ES"},   // Catalan
    {"zh_CN"},   // Chinese
    {"zh_HK"},   // Chinese (Hong Kong)
    {"zh_TW"},   // Chinese (Taiwan)
    {"hr_HR"},   // Croatian
    {"cs_CZ"},   // Czech
    {"da_DK"},   // Danish
    {"nl_NL"},   // Dutch
    {"en_US"},   // English
    {"en_IN"},   // English (India)
    {"en_GB"},   // English (United Kingdom)
    {"et_EE"},   // Estonian
    {"fil_PH"},  // Filipino
    {"fi_FI"},   // Finnish
    {"fr_FR"},   // French
    {"fr_CA"},   // French (Canada)
    {"gl_ES"},   // Galician
    {"ka_GE"},   // Georgian
    {"de_DE"},   // German
    {"el_GR"},   // Greek
    {"gu_IN"},   // Gujarati
    {"he_IL"},   // Hebrew
    {"hi_IN"},   // Hindi
    {"hu_HU"},   // Hungarian
    {"is_IS"},   // Icelandic
    {"id_ID"},   // Indonesian
    {"it_IT"},   // Italian
    {"ja_JP"},   // Japanese
    {"kn_IN"},   // Kannada
    {"kk_KZ"},   // Kazakh
    {"km_KH"},   // Khmer
    {"ko_KR"},   // Korean
    {"ky_KG"},   // Kyrgyz
    {"lo_LA"},   // Lao
    {"lv_LV"},   // Latvian
    {"lt_LT"},   // Lithuanian
    {"mk_MK"},   // Macedonian
    {"ms_MY"},   // Malay
    {"ml_IN"},   // Malayalam
    {"mr_IN"},   // Marathi
    {"mn_MN"},   // Mongolian
    {"my_MM"},   // Myanmar (Burmese)
    {"ne_NP"},   // Nepali
#if BUILDFLAG(IS_COBALT_HERMETIC_BUILD)
    {"no_NO"},        // Norwegian
#endif                // BUILDFLAG(IS_COBALT_HERMETIC_BUILD)
    {"or_IN"},        // Odia
    {"fa_IR"},        // Persian
    {"pl_PL"},        // Polish
    {"pt_BR"},        // Portuguese (Brazil)
    {"pt_PT"},        // Portuguese (Portugal)
    {"pa_IN"},        // Punjabi
    {"ro_RO"},        // Romanian
    {"ru_RU"},        // Russian
    {"sr_RS"},        // Serbian
    {"sr_RS@latin"},  // Serbian (Latin)
    {"si_LK"},        // Sinhala
    {"sk_SK"},        // Slovak
    {"sl_SI"},        // Slovenian
    {"es_MX"},        // Spanish (Latin America)
    {"es_ES"},        // Spanish (Spain)
    {"es_US"},        // Spanish (United States)
    {"sw_KE"},        // Swahili
    {"sv_SE"},        // Swedish
    {"ta_IN"},        // Tamil
    {"te_IN"},        // Telugu
    {"th_TH"},        // Thai
    {"tr_TR"},        // Turkish
    {"uk_UA"},        // Ukrainian
    {"ur_PK"},        // Urdu
    {"uz_UZ"},        // Uzbek
    {"vi_VN"},        // Vietnamese
    {"zu_ZA"}         // Zulu
};

std::string GetLocaleTestName(
    const ::testing::TestParamInfo<LocaleFormatParam>& info) {
  std::string name = info.param.locale_string;
  if (name.empty()) {
    return "Empty";
  }

  // Sanitize the name for use in gtest.
  // First, replace hyphens with "_minus_" to avoid collisions.
  size_t pos = 0;
  while ((pos = name.find('-', pos)) != std::string::npos) {
    name.replace(pos, 1, "_minus_");
    pos += 7;  // length of "_minus_"
  }

  // Then, replace any remaining non-alphanumeric characters (except '_')
  // with '_'.
  std::replace_if(
      name.begin(), name.end(), [](char c) { return !isalnum(c) && c != '_'; },
      '_');
  return name;
}

INSTANTIATE_TEST_SUITE_P(PosixLocaleSetTests,
                         PosixLocaleSetFormatTest,
                         ::testing::ValuesIn([]() {
                           std::vector<LocaleFormatParam> all_formats;
                           all_formats.insert(all_formats.end(),
                                              std::begin(kSpecialLocaleFormats),
                                              std::end(kSpecialLocaleFormats));
                           all_formats.insert(all_formats.end(),
                                              std::begin(kLocaleFormats),
                                              std::end(kLocaleFormats));
                           return all_formats;
                         }()),
                         GetLocaleTestName);

INSTANTIATE_TEST_SUITE_P(PosixLocaleSetTests,
                         PosixLocaleSetAlternativeFormatTest,
                         ::testing::ValuesIn(kLocaleFormats),
                         GetLocaleTestName);
}  // namespace nplb
}  // namespace starboard
