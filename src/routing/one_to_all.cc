#include "nigiri/routing/one_to_all.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include "utl/verify.h"

#include "nigiri/for_each_meta.h"
#include "nigiri/routing/raptor/raptor.h"
#include "nigiri/routing/start_times.h"

namespace nigiri::routing {

constexpr auto const kVias = via_offset_t{0U};

day_idx_t make_base(timetable const& tt, unixtime_t start_time) {
  return day_idx_t{std::chrono::duration_cast<date::days>(
                       std::chrono::round<std::chrono::days>(start_time) -
                       tt.internal_interval().from_)
                       .count()};
}

template <direction SearchDir, bool Rt>
void run_raptor(raptor<SearchDir, Rt, kVias, search_mode::kOneToAll>&& algo,
                timetable const& tt,
                unixtime_t const& start_time,
                query const& q) {
  auto results = pareto_set<journey>{};
  algo.next_start_time();
  for (auto const& s : q.start_) {
    auto const t = SearchDir == direction::kForward ? start_time + s.duration()
                                                    : start_time - s.duration();
    trace("init: time_at_stop={} at {}\n", t, location_idx_t{s.target()});
    nigiri::routing::for_each_meta(
        tt, q.start_match_mode_, s.target(),
        [&](nigiri::location_idx_t const l) { algo.add_start(l, t); });
  }

  // Upper bound: Search journeys faster than 'worst_time_at_dest'
  // It will not find journeys with the same duration
  constexpr auto const kEpsilon = duration_t{1};
  auto const worst_time_at_dest =
      start_time + (SearchDir == direction::kForward ? 1 : -1) *
                       (q.max_travel_time_ + kEpsilon);

  algo.execute(start_time, q.max_transfers_, worst_time_at_dest, q.prf_idx_,
               results);
}

template <direction SearchDir, bool Rt>
raptor_state one_to_all(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q) {
  utl::verify(std::holds_alternative<unixtime_t>(q.start_time_),
              "Start-time must be a time point (unixtime_t)");
  utl::verify(q.via_stops_.empty(),
              "One-to-All search not supported with vias");
  auto const& start_time = std::get<unixtime_t>(q.start_time_);

  auto state = raptor_state{};

  auto is_dest = bitvec{tt.n_locations()};  // Keep footpath time for each stop
  auto is_via = std::array<bitvec, kMaxVias>{};
  auto dist_to_dest = std::vector<std::uint16_t>{};
  auto lb = std::vector<std::uint16_t>(tt.n_locations(), 0U);
  auto const base = make_base(tt, start_time);
  auto const is_wheelchair = q.prf_idx_ == kWheelchairProfile;

  auto r = raptor<SearchDir, Rt, kVias, search_mode::kOneToAll>{
      tt,
      rtt,
      state,
      is_dest,
      is_via,
      dist_to_dest,
      q.td_dest_,
      lb,
      q.via_stops_,
      base,
      q.allowed_claszes_,
      q.require_bike_transport_,
      q.require_car_transport_,
      is_wheelchair,
      q.transfer_time_settings_,
      q.max_transfers_};

  run_raptor(std::move(r), tt, start_time, q);

  return state;
}

template <direction SearchDir, bool Rt>
std::vector<std::pair<location_idx_t, duration_t>> one_to_all_window(
    timetable const& tt, rt_timetable const* rtt, query const& q) {
  constexpr auto const kFwd = SearchDir == direction::kForward;
  utl::verify(std::holds_alternative<interval<unixtime_t>>(q.start_time_),
              "Start-time must be an interval");
  utl::verify(q.via_stops_.empty(),
              "One-to-All search not supported with vias");
  auto const& window = std::get<interval<unixtime_t>>(q.start_time_);
  if (window.from_ >= window.to_) {
    return {};
  }

  // Start times: when a start location catches a departure (reaches an
  // arrival), latest first (earliest first for kBackward), and the window's
  // last (first) minute, the best start for journeys that begin with a
  // departure outside the window.
  auto starts = std::vector<start>{};
  get_starts(SearchDir, tt, rtt, window, q.start_, q.td_start_, {},
             q.max_start_offset_, q.start_match_mode_, false, starts, false,
             q.prf_idx_, q.transfer_time_settings_);
  auto start_times = std::vector<unixtime_t>{};
  start_times.reserve(starts.size() + 1U);
  for (auto const& s : starts) {
    if (window.contains(s.time_at_start_)) {
      start_times.push_back(s.time_at_start_);
    }
  }
  start_times.push_back(kFwd ? window.to_ - duration_t{1} : window.from_);
  std::sort(begin(start_times), end(start_times),
            [](unixtime_t const a, unixtime_t const b) {
              return kFwd ? a > b : a < b;
            });
  start_times.erase(std::unique(begin(start_times), end(start_times)),
                    end(start_times));

  auto state = raptor_state{};
  auto is_dest = bitvec{tt.n_locations()};
  auto is_via = std::array<bitvec, kMaxVias>{};
  auto dist_to_dest = std::vector<std::uint16_t>{};
  auto lb = std::vector<std::uint16_t>(tt.n_locations(), 0U);
  auto const base = make_base(tt, window.from_);
  auto algo = raptor<SearchDir, Rt, kVias, search_mode::kOneToAll>{
      tt,
      rtt,
      state,
      is_dest,
      is_via,
      dist_to_dest,
      q.td_dest_,
      lb,
      q.via_stops_,
      base,
      q.allowed_claszes_,
      q.require_bike_transport_,
      q.require_car_transport_,
      q.prf_idx_ == kWheelchairProfile,
      q.transfer_time_settings_,
      q.max_transfers_};

  // best_ holds, after each search, the earliest arrival (latest departure)
  // from that start time, the ones found for later (earlier) start times
  // included: waiting at the start is always possible.
  auto const best = state.get_best<kVias>();
  auto const base_day =
      tt.internal_interval_days().from_ + static_cast<int>(to_idx(base)) *
                                              date::days{1};
  auto fastest =
      std::vector<duration_t>(tt.n_locations(), duration_t::max());
  auto results = pareto_set<journey>{};
  for (auto const t : start_times) {
    algo.next_start_time();
    for (auto const& s : q.start_) {
      auto const at = kFwd ? t + s.duration() : t - s.duration();
      for_each_meta(tt, q.start_match_mode_, s.target(),
                    [&](location_idx_t const l) { algo.add_start(l, at); });
    }
    // As one_to_all: journeys up to max_travel_time_ (see run_raptor).
    auto const worst_time_at_dest =
        t + (kFwd ? 1 : -1) * (q.max_travel_time_ + duration_t{1});
    algo.execute(t, q.max_transfers_, worst_time_at_dest, q.prf_idx_,
                 results);

    for (auto i = 0U; i != tt.n_locations(); ++i) {
      if (best[i][0] == kInvalidDelta<SearchDir>) {
        continue;
      }
      auto const at = delta_to_unix(base_day, best[i][0]);
      auto const d = kFwd ? at - t : t - at;
      if (d <= q.max_travel_time_ && d < fastest[i]) {
        fastest[i] = std::chrono::duration_cast<duration_t>(d);
      }
    }
  }

  auto reached = std::vector<std::pair<location_idx_t, duration_t>>{};
  for (auto i = 0U; i != tt.n_locations(); ++i) {
    if (fastest[i] != duration_t::max()) {
      reached.emplace_back(location_idx_t{i}, fastest[i]);
    }
  }
  return reached;
}

template <direction SearchDir>
raptor_state one_to_all(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q) {
  if (rtt == nullptr) {
    return one_to_all<SearchDir, false>(tt, rtt, q);
  } else {
    return one_to_all<SearchDir, true>(tt, rtt, q);
  }
}

template <direction SearchDir>
std::vector<std::pair<location_idx_t, duration_t>> one_to_all_window(
    timetable const& tt, rt_timetable const* rtt, query const& q) {
  if (rtt == nullptr) {
    return one_to_all_window<SearchDir, false>(tt, rtt, q);
  } else {
    return one_to_all_window<SearchDir, true>(tt, rtt, q);
  }
}

fastest_offset get_fastest_one_to_all_offsets(timetable const& tt,
                                              raptor_state const& state,
                                              direction const search_dir,
                                              location_idx_t const l,
                                              unixtime_t const start_time,
                                              std::uint8_t const transfers) {
  auto const invalid_delta = search_dir == direction::kForward
                                 ? kInvalidDelta<direction::kForward>
                                 : kInvalidDelta<direction::kBackward>;
  auto const& round_times = state.get_round_times<kVias>();
  auto const n_rounds = static_cast<std::uint8_t>(
      std::min(static_cast<unsigned>(transfers) + 2U, state.n_rounds()));
  for (auto const k : std::views::iota(std::uint8_t{0U}, n_rounds)  //
                          | std::views::reverse) {
    if (round_times[k][to_idx(l)][kVias] != invalid_delta) {
      auto const base =
          tt.internal_interval_days().from_ +
          static_cast<int>(to_idx(make_base(tt, start_time))) * date::days{1};
      auto end_time = delta_to_unix(base, round_times[k][to_idx(l)][0]);
      return {
          .duration_ = static_cast<delta_t>((end_time - start_time).count()),
          .k_ = k,
      };
    }
  }
  return {};
};

void for_each_one_to_all_round_time(
    timetable const& tt,
    raptor_state const& state,
    direction const search_dir,
    location_idx_t const l,
    unixtime_t const start_time,
    std::uint8_t const transfers,
    std::function<void(std::uint8_t, duration_t)> const& cb) {
  auto const invalid_delta = search_dir == direction::kForward
                                 ? kInvalidDelta<direction::kForward>
                                 : kInvalidDelta<direction::kBackward>;
  auto const& round_times = state.get_round_times<kVias>();
  auto const base =
      tt.internal_interval_days().from_ +
      static_cast<int>(to_idx(make_base(tt, start_time))) * date::days{1};
  auto const n_rounds = static_cast<std::uint8_t>(
      std::min(static_cast<unsigned>(transfers) + 2U, state.n_rounds()));
  for (auto const k : std::views::iota(std::uint8_t{0U}, n_rounds)) {
    if (round_times[k][to_idx(l)][kVias] != invalid_delta) {
      auto end_time = delta_to_unix(base, round_times[k][to_idx(l)][kVias]);
      cb(k, search_dir == direction::kForward ? end_time - start_time
                                              : start_time - end_time);
    }
  }
}

template raptor_state one_to_all<direction::kForward>(timetable const&,
                                                      rt_timetable const*,
                                                      query const&);
template raptor_state one_to_all<direction::kBackward>(timetable const&,
                                                       rt_timetable const*,
                                                       query const&);

template std::vector<std::pair<location_idx_t, duration_t>>
one_to_all_window<direction::kForward>(timetable const&,
                                       rt_timetable const*,
                                       query const&);
template std::vector<std::pair<location_idx_t, duration_t>>
one_to_all_window<direction::kBackward>(timetable const&,
                                        rt_timetable const*,
                                        query const&);

}  // namespace nigiri::routing
