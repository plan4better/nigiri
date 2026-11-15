#pragma once

#include <array>
#include <cassert>
#include <span>
#include <vector>

#include "date/date.h"

#include "cista/containers/bitvec.h"
#include "cista/containers/flat_matrix.h"

#include "nigiri/common/delta_t.h"
#include "nigiri/common/flat_matrix_view.h"
#include "nigiri/routing/limits.h"

namespace nigiri {
struct timetable;
}

namespace nigiri::routing {

struct raptor_state {
  raptor_state() = default;
  raptor_state(raptor_state const&) = delete;
  raptor_state& operator=(raptor_state const&) = delete;
  raptor_state(raptor_state&&) = default;
  raptor_state& operator=(raptor_state&&) = default;
  ~raptor_state() = default;

  // Sizes the state for a search. The per-location storage holds one slot per
  // via stage (`n_vias` + 1) and, for round times, one row per round
  // (`max_transfers` + 2). The defaults fit every search; a search without
  // vias or with fewer transfers can ask for less: for a large timetable most
  // of the state is otherwise reserved and never used.
  raptor_state& resize(unsigned n_locations,
                       unsigned n_routes,
                       unsigned n_rt_transports,
                       via_offset_t n_vias = kMaxVias,
                       std::uint8_t max_transfers = kMaxTransfers);

  // Rounds (max_transfers + 2) the round times are sized for.
  unsigned n_rounds() const { return n_rounds_; }

  template <via_offset_t Vias>
  void print(timetable const& tt, date::sys_days, delta_t invalid);

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1>> get_tmp() {
    assert(Vias <= n_vias_);
    return {
        reinterpret_cast<std::array<delta_t, Vias + 1>*>(tmp_storage_.data()),
        n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1> const> get_tmp() const {
    assert(Vias <= n_vias_);
    return {reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                tmp_storage_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1>> get_best() {
    assert(Vias <= n_vias_);
    return {
        reinterpret_cast<std::array<delta_t, Vias + 1>*>(best_storage_.data()),
        n_locations_};
  }

  template <via_offset_t Vias>
  std::span<std::array<delta_t, Vias + 1> const> get_best() const {
    assert(Vias <= n_vias_);
    return {reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                best_storage_.data()),
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1>> get_round_times() {
    assert(Vias <= n_vias_);
    return {{reinterpret_cast<std::array<delta_t, Vias + 1>*>(
                 round_times_storage_.data()),
             n_locations_ * n_rounds_},
            n_rounds_,
            n_locations_};
  }

  template <via_offset_t Vias>
  flat_matrix_view<std::array<delta_t, Vias + 1> const> get_round_times()
      const {
    assert(Vias <= n_vias_);
    return {{reinterpret_cast<std::array<delta_t, Vias + 1> const*>(
                 round_times_storage_.data()),
             n_locations_ * n_rounds_},
            n_rounds_,
            n_locations_};
  }

  unsigned n_locations_{};
  via_offset_t n_vias_{kMaxVias};
  unsigned n_rounds_{kMaxTransfers + 2U};
  std::vector<delta_t> tmp_storage_;
  std::vector<delta_t> best_storage_;
  std::vector<delta_t> round_times_storage_;
  bitvec station_mark_;
  bitvec prev_station_mark_;
  bitvec route_mark_;
  bitvec rt_transport_mark_;
};

}  // namespace nigiri::routing
