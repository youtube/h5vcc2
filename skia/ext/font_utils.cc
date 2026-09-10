// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skia/ext/font_utils.h"

#include "base/check.h"
#include "build/build_config.h"
#include "skia/fontations_feature.h"
#include "third_party/skia/include/core/SkFont.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkTypeface.h"

#if BUILDFLAG(IS_ANDROID)
#include "base/base_paths_android.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "third_party/skia/include/ports/SkFontMgr_android.h"
#include "third_party/skia/include/ports/SkFontScanner_Fontations.h"
#endif

#if BUILDFLAG(IS_APPLE)
#include "third_party/skia/include/ports/SkFontMgr_mac_ct.h"
#endif

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
#include "third_party/skia/include/ports/SkFontConfigInterface.h"
#include "third_party/skia/include/ports/SkFontMgr_FontConfigInterface.h"
#include "third_party/skia/include/ports/SkFontScanner_Fontations.h"
#endif

#if BUILDFLAG(IS_FUCHSIA)
#include <fuchsia/fonts/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include "base/fuchsia/process_context.h"
#include "third_party/skia/include/ports/SkFontMgr_fuchsia.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "third_party/skia/include/ports/SkTypeface_win.h"
#endif

#if defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
#include "third_party/skia/include/ports/SkFontMgr_empty.h"
#endif

#if BUILDFLAG(IS_COBALT_HERMETIC_BUILD)
#include "cobalt/renderer/rasterizer/skia/skia/src/ports/SkFontMgr_cobalt.h"
#endif

#include <mutex>

namespace {

bool g_factory_called = false;

// This is a purposefully leaky pointer that has ownership of the FontMgr.
SkFontMgr* g_fontmgr_override = nullptr;

}  // namespace

namespace skia {

static sk_sp<SkFontMgr> fontmgr_factory() {
  if (g_fontmgr_override) {
    return sk_ref_sp(g_fontmgr_override);
  }
#if BUILDFLAG(IS_COBALT_HERMETIC_BUILD)
  return SkFontMgr_New_Cobalt();
#else
#if BUILDFLAG(IS_ANDROID)
#if BUILDFLAG(IS_COBALT)
  // When Cobalt custom Android fonts XML is enabled, configure Skia to use
  // hermetic custom XML font fallbacks (`cobalt_android_fonts.xml`) extracted
  // into the app data directory.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "use-custom-android-fonts-xml")) {
    base::FilePath app_data_dir;
    if (base::PathService::Get(base::DIR_ANDROID_APP_DATA, &app_data_dir)) {
      std::string xml_path = app_data_dir.Append("storage").Append("cobalt_android_fonts.xml").value();
      SkFontMgr_Android_CustomFonts custom_fonts;
      custom_fonts.fSystemFontUse = SkFontMgr_Android_CustomFonts::kPreferCustom;
      custom_fonts.fBasePath = "/system/fonts/";
      custom_fonts.fFontsXml = xml_path.c_str();
      custom_fonts.fFallbackFontsXml = nullptr;
      custom_fonts.fIsolated = true;
      if (base::FeatureList::IsEnabled(skia::kFontationsAndroidSystemFonts)) {
        return SkFontMgr_New_Android(&custom_fonts, SkFontScanner_Make_Fontations());
      } else {
        return SkFontMgr_New_Android(&custom_fonts);
      }
    }
  }
#endif // BUILDFLAG (IS_COBALT)
  if (base::FeatureList::IsEnabled(skia::kFontationsAndroidSystemFonts)) {
    return SkFontMgr_New_Android(nullptr, SkFontScanner_Make_Fontations());
  } else {
    return SkFontMgr_New_Android(nullptr);
  }
#elif BUILDFLAG(IS_APPLE)
  return SkFontMgr_New_CoreText(nullptr);
#elif BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_LINUX)
  sk_sp<SkFontConfigInterface> fci(SkFontConfigInterface::RefGlobal());
  if (base::FeatureList::IsEnabled(skia::kFontationsLinuxSystemFonts)) {
    return fci ? SkFontMgr_New_FCI(std::move(fci),
                                   SkFontScanner_Make_Fontations())
               : nullptr;
  } else {
    return fci ? SkFontMgr_New_FCI(std::move(fci)) : nullptr;
  }
#elif BUILDFLAG(IS_FUCHSIA)
  fuchsia::fonts::ProviderSyncPtr provider;
  base::ComponentContextForProcess()->svc()->Connect(provider.NewRequest());
  return SkFontMgr_New_Fuchsia(std::move(provider));
#elif BUILDFLAG(IS_WIN)
  return SkFontMgr_New_DirectWrite();
#elif defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
  return SkFontMgr_New_Custom_Empty();
#else
  return SkFontMgr::RefEmpty();
#endif
#endif
}

sk_sp<SkFontMgr> DefaultFontMgr() {
  static std::once_flag flag;
  static sk_sp<SkFontMgr> mgr;
  std::call_once(flag, [] {
    mgr = fontmgr_factory();
    g_factory_called = true;
  });
  return mgr;
}

void OverrideDefaultSkFontMgr(sk_sp<SkFontMgr> fontmgr) {
  CHECK(!g_factory_called);

  SkSafeUnref(g_fontmgr_override);
  g_fontmgr_override = fontmgr.release();
}

sk_sp<SkTypeface> MakeTypefaceFromName(const char* name, SkFontStyle style) {
  sk_sp<SkFontMgr> fm = DefaultFontMgr();
  CHECK(fm);
  sk_sp<SkTypeface> face = fm->legacyMakeTypeface(name, style);
  return face;
}

sk_sp<SkTypeface> DefaultTypeface() {
  sk_sp<SkTypeface> face = MakeTypefaceFromName(nullptr, SkFontStyle());
  if (face) {
    return face;
  }
  // Due to how SkTypeface::MakeDefault() used to work, many callers of this
  // depend on the returned SkTypeface being non-null. An empty Typeface is
  // non-null, but has no glyphs.
  face = SkTypeface::MakeEmpty();
  CHECK(face);
  return face;
}

SkFont DefaultFont() {
  return SkFont(DefaultTypeface());
}

}  // namespace skia
