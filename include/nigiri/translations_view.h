#pragma once

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>

#include "nigiri/timetable.h"

namespace nigiri {

inline auto get_translation_view(timetable const& tt,
                                 translation_idx_t const t) {
  namespace sv = std::views;
  auto const& languages = tt.translation_language_[t];
  auto const& translations = tt.translations_[t];
  auto const n = std::min(languages.size(), translations.size());
  auto const lang_begin = languages.begin();
  auto const text_begin = translations.begin();
  return sv::iota(std::size_t{0}, n) |
         sv::transform([=](std::size_t const i) {
           return std::pair{*(lang_begin + i), (*(text_begin + i)).view()};
         });
}

}  // namespace nigiri