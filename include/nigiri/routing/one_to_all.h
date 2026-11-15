#pragma once

#include <limits>
#include <utility>
#include <vector>

#include "nigiri/common/delta_t.h"
#include "nigiri/routing/query.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/rt/rt_timetable.h"
#include "nigiri/timetable.h"
#include "nigiri/types.h"

namespace nigiri::routing {

struct fastest_offset {
  delta_t duration_{std::numeric_limits<delta_t>::max()};
  // k == 0: No connection
  // k == 1: Direct connection
  // k == 2: Connection with 1 transfer
  std::uint8_t k_{std::numeric_limits<std::uint8_t>::max()};
};

// One search from q.start_time_ (a time point). The returned state holds
// round times for q.max_transfers_ + 2 rounds and no via stages.
template <direction SearchDir>
raptor_state one_to_all(timetable const& tt,
                        rt_timetable const* rtt,
                        query const& q);

// The fastest travel time to every location over a window of start times:
// the minimum over one one_to_all per minute of the window, computed as one
// rRAPTOR search (as routing::search does for journeys). Start times run
// from the latest to the earliest (earliest to latest for kBackward) on one
// raptor state: arrival times found for a later start bound the search from
// an earlier one, and the state is allocated and reset once, not per minute.
//
// q.start_time_ is the window, an interval: departure times at the start
// for kForward, arrival times at the target for kBackward. The only start
// times searched are those at which a start location can catch a departure
// (reach an arrival), plus the window's last (first) minute: the fastest
// time to any location is reached at one of them. As one_to_all, starts
// are q.start_ (meta stations matched per q.start_match_mode_) without start
// footpaths. Returns the locations reached within q.max_travel_time_, start
// locations included, with their fastest time.
template <direction SearchDir>
std::vector<std::pair<location_idx_t, duration_t>> one_to_all_window(
    timetable const& tt, rt_timetable const* rtt, query const& q);

fastest_offset get_fastest_one_to_all_offsets(timetable const& tt,
                                              raptor_state const& state,
                                              direction,
                                              location_idx_t,
                                              unixtime_t start_time,
                                              std::uint8_t transfers);

void for_each_one_to_all_round_time(
    timetable const&,
    raptor_state const&,
    direction,
    location_idx_t,
    unixtime_t start_time,
    std::uint8_t transfers,
    std::function<void(std::uint8_t, duration_t)> const&);

}  // namespace nigiri::routing
