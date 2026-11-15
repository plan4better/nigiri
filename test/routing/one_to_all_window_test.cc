#include "gtest/gtest.h"

#include <limits>
#include <map>

#include "nigiri/loader/hrd/load_timetable.h"
#include "nigiri/loader/init_finish.h"
#include "nigiri/routing/one_to_all.h"

#include "../loader/hrd/hrd_timetable.h"

using namespace date;
using namespace nigiri;
using namespace nigiri::loader;
using namespace nigiri::routing;
using namespace nigiri::test_data::hrd_timetable;

namespace {

timetable load_abc() {
  constexpr auto const src = source_idx_t{0U};
  auto tt = timetable{};
  tt.date_range_ = full_period();
  load_timetable(src, loader::hrd::hrd_5_20_26, files_abc(), tt);
  finalize(tt);
  return tt;
}

location_idx_t stop_idx(timetable const& tt, std::string_view const id) {
  return tt.locations_.location_id_to_idx_.at({id, source_idx_t{0U}});
}

// The fastest time per location over one one_to_all per minute of the
// window: what one_to_all_window must return.
template <direction SearchDir>
std::map<location_idx_t, duration_t> fastest_per_minute(
    timetable const& tt,
    query q,
    interval<unixtime_t> const window) {
  auto fastest = std::map<location_idx_t, duration_t>{};
  for (auto t = window.from_; t < window.to_; t += duration_t{1}) {
    q.start_time_ = t;
    auto const state = one_to_all<SearchDir>(tt, nullptr, q);
    for (auto l = 0U; l != tt.n_locations(); ++l) {
      auto const f = get_fastest_one_to_all_offsets(
          tt, state, SearchDir, location_idx_t{l}, t, q.max_transfers_);
      if (f.k_ == std::numeric_limits<std::uint8_t>::max()) {
        continue;
      }
      auto const d = duration_t{SearchDir == direction::kForward
                                    ? f.duration_
                                    : -f.duration_};
      if (d > q.max_travel_time_) {
        continue;
      }
      auto const it = fastest.find(location_idx_t{l});
      if (it == end(fastest) || d < it->second) {
        fastest[location_idx_t{l}] = d;
      }
    }
  }
  return fastest;
}

// `by_trip`: a location only a trip reaches, so the case is not only start
// locations and their offsets.
template <direction SearchDir>
void expect_window_equals_per_minute(timetable const& tt,
                                     query q,
                                     interval<unixtime_t> const window,
                                     location_idx_t const by_trip) {
  auto const expected = fastest_per_minute<SearchDir>(tt, q, window);
  EXPECT_TRUE(expected.contains(by_trip));
  q.start_time_ = window;
  auto actual = std::map<location_idx_t, duration_t>{};
  for (auto const& [l, d] : one_to_all_window<SearchDir>(tt, nullptr, q)) {
    actual[l] = d;
  }
  EXPECT_EQ(expected, actual);
}

}  // namespace

TEST(routing, one_to_all_window_forward) {
  auto const tt = load_abc();
  auto const day = unixtime_t{sys_days{2020_y / March / 30}};
  for (auto const max_transfers : {std::uint8_t{0U}, std::uint8_t{1U}}) {
    for (auto const max_travel : {duration_t{90}, duration_t{180}}) {
      auto const q = query{
          .start_ = {{stop_idx(tt, "0000001"), 0_minutes, 0U},
                     {stop_idx(tt, "0000002"), 7_minutes, 0U}},
          .max_transfers_ = max_transfers,
          .max_travel_time_ = max_travel};
      expect_window_equals_per_minute<direction::kForward>(
          tt, q, interval{day + 4_hours + 50_minutes, day + 6_hours},
          stop_idx(tt, "0000003"));
    }
  }
}

TEST(routing, one_to_all_window_backward) {
  auto const tt = load_abc();
  auto const day = unixtime_t{sys_days{2020_y / March / 30}};
  for (auto const max_transfers : {std::uint8_t{0U}, std::uint8_t{1U}}) {
    auto const q = query{.start_ = {{stop_idx(tt, "0000003"), 0_minutes, 0U}},
                         .max_transfers_ = max_transfers,
                         .max_travel_time_ = duration_t{180}};
    expect_window_equals_per_minute<direction::kBackward>(
        tt, q, interval{day + 6_hours + 50_minutes, day + 8_hours},
        stop_idx(tt, "0000002"));
  }
}

TEST(routing, one_to_all_state_sized_for_query) {
  auto const tt = load_abc();
  auto const q =
      query{.start_time_ = unixtime_t{sys_days{2020_y / March / 30}} + 5_hours,
            .start_ = {{stop_idx(tt, "0000001"), 0_minutes, 0U}},
            .max_transfers_ = 1U};
  auto const state = one_to_all<direction::kForward>(tt, nullptr, q);
  EXPECT_EQ(3U, state.n_rounds());

  // Asking for more transfers than the state holds reads what it has.
  auto const f = get_fastest_one_to_all_offsets(
      tt, state, direction::kForward, stop_idx(tt, "0000003"),
      std::get<unixtime_t>(q.start_time_), kMaxTransfers);
  EXPECT_EQ(2U, f.k_);
}
