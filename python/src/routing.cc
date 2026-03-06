#include "pybind_common.h"

#include "nigiri/routing/query.h"
#include "nigiri/routing/journey.h"
#include "nigiri/routing/raptor_search.h"
#include "nigiri/routing/clasz_mask.h"
#include "nigiri/routing/limits.h"
#include "nigiri/routing/one_to_all.h"
#include "nigiri/routing/raptor/raptor_state.h"
#include "nigiri/routing/search.h"
#include "nigiri/rt/frun.h"
#include "nigiri/timetable.h"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace py = pybind11;
using namespace nigiri;
using namespace nigiri::routing;

namespace {

std::string leg_kind(journey::leg const& leg) {
  if (std::holds_alternative<journey::run_enter_exit>(leg.uses_)) {
    return "transport";
  }
  if (std::holds_alternative<footpath>(leg.uses_)) {
    return "footpath";
  }
  return "offset";
}

py::object get_transport_info(journey::leg const& leg,
                              timetable const& tt,
                              rt_timetable const* rtt) {
  auto const* run_info = std::get_if<journey::run_enter_exit>(&leg.uses_);
  if (run_info == nullptr) {
    return py::none();
  }

  auto const fr = rt::frun{tt, rtt, run_info->r_};
  auto const first_stop = fr[run_info->stop_range_.from_];
  auto const last_stop = fr[run_info->stop_range_.to_ - 1U];
  auto const trip_id = fr.id();

  py::dict info;
  info["is_rt"] = py::bool_(run_info->r_.is_rt());
  info["is_scheduled"] = py::bool_(run_info->r_.is_scheduled());
  info["trip_id"] = py::str(trip_id.id_);
  info["trip_source"] = py::int_(trip_id.src_.v_);
  info["run_name"] = py::str(fr.name(std::nullopt));
  info["route_id"] = py::str(first_stop.get_route_id(event_type::kDep));
  info["route_short_name"] =
      py::str(first_stop.route_short_name(event_type::kDep, std::nullopt));
  info["route_long_name"] =
      py::str(first_stop.route_long_name(event_type::kDep, std::nullopt));
  info["display_name"] =
      py::str(first_stop.display_name(event_type::kDep, std::nullopt));
  info["from_stop_id"] = py::str(first_stop.get_location_id());
  info["to_stop_id"] = py::str(last_stop.get_location_id());
  info["stop_range_from"] = py::int_(run_info->stop_range_.from_);
  info["stop_range_to"] = py::int_(run_info->stop_range_.to_);
  if (run_info->r_.t_.is_valid()) {
    info["transport_idx"] = py::int_(run_info->r_.t_.t_idx_.v_);
    info["day_idx"] = py::int_(run_info->r_.t_.day_.v_);
  } else {
    info["transport_idx"] = py::none();
    info["day_idx"] = py::none();
  }
  if (run_info->r_.rt_ != rt_transport_idx_t::invalid()) {
    info["rt_transport_idx"] = py::int_(run_info->r_.rt_.v_);
  } else {
    info["rt_transport_idx"] = py::none();
  }

  return std::move(info);
}

py::object get_footpath_info(journey::leg const& leg) {
  auto const* fp = std::get_if<footpath>(&leg.uses_);
  if (fp == nullptr) {
    return py::none();
  }

  py::dict info;
  info["duration"] = py::int_(fp->duration().count());
  info["target"] = py::int_(fp->target().v_);
  return std::move(info);
}

py::object get_offset_info(journey::leg const& leg) {
  auto const* off = std::get_if<offset>(&leg.uses_);
  if (off == nullptr) {
    return py::none();
  }

  py::dict info;
  info["duration"] = py::int_(off->duration().count());
  info["target"] = py::int_(off->target().v_);
  info["transport_mode_id"] = py::int_(off->type());
  return std::move(info);
}

unixtime_t get_one_to_all_start_time(query const& q) {
  if (!std::holds_alternative<unixtime_t>(q.start_time_)) {
    throw std::runtime_error(
        "one_to_all requires query.start_time to be a single time point");
  }
  return std::get<unixtime_t>(q.start_time_);
}

}  // namespace

void init_routing(py::module_& m) {
  // Transport mode ID - just return the int directly
  m.def("TransportModeId", [](std::uint32_t id) { return id; },
        py::arg("id"), "Create a transport mode ID (returns uint32)");


  // Offset
  py::class_<offset>(m, "Offset")
      .def(py::init([](location_idx_t loc, int minutes, transport_mode_id_t mode) {
        return offset{loc, duration_t{i32_minutes{minutes}}, mode};
      }),
           py::arg("target"),
           py::arg("duration"),
           py::arg("transport_mode") = transport_mode_id_t{0})
      .def("target", &offset::target)
      .def("duration", [](offset const& o) { return o.duration().count(); })
      .def("type", &offset::type)
      .def(py::self == py::self)
      .def(py::self < py::self)
      .def("__repr__", [](offset const& o) {
        return "Offset(target=" + std::to_string(o.target().v_) +
               ", duration=" + std::to_string(o.duration().count()) + ")";
      });

  // TD offset
  py::class_<td_offset>(m, "TdOffset")
      .def(py::init<>())
      .def_readwrite("valid_from", &td_offset::valid_from_)
      .def_readwrite("duration", &td_offset::duration_)
      .def_readwrite("transport_mode_id", &td_offset::transport_mode_id_)
      .def("duration_fn", &td_offset::duration)
      .def(py::self == py::self)
      .def("__repr__", [](td_offset const& o) {
        return "TdOffset(duration=" + std::to_string(o.duration_.count()) + ")";
      });

  // Via stop
  py::class_<via_stop>(m, "ViaStop")
      .def(py::init<>())
      .def_readwrite("location", &via_stop::location_)
      .def_property("stay",
          [](via_stop const& vs) { return py::int_(vs.stay_.count()); },
          [](via_stop& vs, int minutes) { vs.stay_ = duration_t{minutes}; })
      .def(py::self == py::self)
      .def("__repr__", [](via_stop const& vs) {
        return "ViaStop(location=" + std::to_string(vs.location_.v_) +
               ", stay=" + std::to_string(vs.stay_.count()) + ")";
      });

  // Location match mode
  py::enum_<location_match_mode>(m, "LocationMatchMode")
      .value("EXACT", location_match_mode::kExact)
      .value("ONLY_CHILDREN", location_match_mode::kOnlyChildren)
      .value("EQUIVALENT", location_match_mode::kEquivalent)
      .value("INTERMODAL", location_match_mode::kIntermodal)
      .export_values();

  // Clasz mask
  m.def("all_clasz_allowed", &all_clasz_allowed, "Get mask allowing all classes");

  // Transfer time settings
  py::class_<transfer_time_settings>(m, "TransferTimeSettings")
      .def(py::init<>())
      .def_readwrite("default", &transfer_time_settings::default_)
      .def_readwrite("min_transfer_time", &transfer_time_settings::min_transfer_time_)
      .def_readwrite("additional_time", &transfer_time_settings::additional_time_)
      .def_readwrite("factor", &transfer_time_settings::factor_)
      .def("__repr__", [](transfer_time_settings const&) {
        return "TransferTimeSettings()";
      });

  // Query
  py::class_<query>(m, "Query")
      .def(py::init<>())
      
      // Start time - expose as int (minutes) or tuple of ints (interval)
      .def_property("start_time",
        [](query const& q) -> py::object {
          if (std::holds_alternative<unixtime_t>(q.start_time_)) {
            auto ut = std::get<unixtime_t>(q.start_time_);
            return py::int_(ut.time_since_epoch().count());
          } else {
            auto const& iv = std::get<interval<unixtime_t>>(q.start_time_);
            return py::make_tuple(
              iv.from_.time_since_epoch().count(),
              iv.to_.time_since_epoch().count()
            );
          }
        },
        [](query& q, py::handle obj) {
          if (py::isinstance<py::tuple>(obj)) {
            auto t = obj.cast<py::tuple>();
            q.start_time_ = interval<unixtime_t>{
              unixtime_t{i32_minutes{t[0].cast<int>()}},
              unixtime_t{i32_minutes{t[1].cast<int>()}}
            };
          } else {
            q.start_time_ = unixtime_t{
              i32_minutes{obj.cast<int>()}
            };
          }
        })
      
      .def_readwrite("start_match_mode", &query::start_match_mode_)
      .def_readwrite("dest_match_mode", &query::dest_match_mode_)
      .def_readwrite("use_start_footpaths", &query::use_start_footpaths_)
      .def_readwrite("start", &query::start_)
      .def_readwrite("destination", &query::destination_)
      .def_readwrite("max_start_offset", &query::max_start_offset_)
      .def_readwrite("max_transfers", &query::max_transfers_)
      .def_property("max_travel_time",
          [](query const& q) { return py::int_(q.max_travel_time_.count()); },
          [](query& q, int minutes) { q.max_travel_time_ = duration_t{minutes}; })
      .def_readwrite("min_connection_count", &query::min_connection_count_)
      .def_readwrite("extend_interval_earlier", &query::extend_interval_earlier_)
      .def_readwrite("extend_interval_later", &query::extend_interval_later_)
      .def_readwrite("prf_idx", &query::prf_idx_)
      .def_readwrite("allowed_claszes", &query::allowed_claszes_)
      .def_readwrite("require_bike_transport", &query::require_bike_transport_)
      .def_readwrite("require_car_transport", &query::require_car_transport_)
      .def_readwrite("transfer_time_settings", &query::transfer_time_settings_)
      .def_readwrite("via_stops", &query::via_stops_)
      .def_readwrite("slow_direct", &query::slow_direct_)
      
      .def("flip_dir", &query::flip_dir, "Flip query direction")
      .def(py::self == py::self)
      
      .def("__repr__", [](query const& q) {
        return "Query(start=" + std::to_string(q.start_.size()) +
               " locations, dest=" + std::to_string(q.destination_.size()) +
               " locations, max_transfers=" + std::to_string(q.max_transfers_) + ")";
      });

  // Journey leg
  py::class_<journey::leg>(m, "Leg")
      .def_readonly("from", &journey::leg::from_)
      .def_readonly("to", &journey::leg::to_)
      // Expose times as integers (minutes since epoch)
      .def_property_readonly("dep_time",
        [](journey::leg const& l) {
          return l.dep_time_.time_since_epoch().count();
        })
      .def_property_readonly("arr_time",
        [](journey::leg const& l) {
          return l.arr_time_.time_since_epoch().count();
        })
      .def("duration", [](journey::leg const& l) {
        return std::abs((l.arr_time_ - l.dep_time_).count());
      })
      .def_property_readonly("kind", [](journey::leg const& l) {
        return leg_kind(l);
      })
      .def("is_transport", [](journey::leg const& l) {
        return std::holds_alternative<journey::run_enter_exit>(l.uses_);
      })
      .def("is_footpath", [](journey::leg const& l) {
        return std::holds_alternative<footpath>(l.uses_);
      })
      .def("is_offset", [](journey::leg const& l) {
        return std::holds_alternative<offset>(l.uses_);
      })
      .def("transport_info", &get_transport_info,
           py::arg("timetable"), py::arg("rt_timetable") = nullptr,
           "Return transit leg metadata or None for non-transport legs")
      .def("footpath_info", &get_footpath_info,
           "Return footpath metadata or None")
      .def("offset_info", &get_offset_info,
           "Return offset (MUMO) metadata or None")
      .def("from_id", [](journey::leg const& l, timetable const& tt) {
        return std::string(tt.locations_.ids_[l.from_].view());
      }, py::arg("timetable"))
      .def("to_id", [](journey::leg const& l, timetable const& tt) {
        return std::string(tt.locations_.ids_[l.to_].view());
      }, py::arg("timetable"))
      .def("from_name", [](journey::leg const& l, timetable const& tt) {
        return std::string(tt.get_default_name(l.from_));
      }, py::arg("timetable"))
      .def("to_name", [](journey::leg const& l, timetable const& tt) {
        return std::string(tt.get_default_name(l.to_));
      }, py::arg("timetable"))
      .def(py::self == py::self)
      .def(py::self < py::self)
      .def("__repr__", [](journey::leg const& leg) {
        return "Leg(from=" + std::to_string(leg.from_.v_) +
               ", to=" + std::to_string(leg.to_.v_) +
               ", dep=" + std::to_string(leg.dep_time_.time_since_epoch().count()) +
               ", arr=" + std::to_string(leg.arr_time_.time_since_epoch().count()) +
               ", kind=" + leg_kind(leg) + ")";
      });

  // Journey
  py::class_<journey>(m, "Journey")
      .def(py::init<>())
      .def_readonly("legs", &journey::legs_)
      // Expose times as integers (minutes since epoch)
      .def_property_readonly("start_time",
        [](journey const& j) {
          return j.start_time_.time_since_epoch().count();
        })
      .def_property_readonly("dest_time",
        [](journey const& j) {
          return j.dest_time_.time_since_epoch().count();
        })
      .def_readonly("transfers", &journey::transfers_)
      .def_readonly("destination", &journey::dest_)
      .def_readonly("error", &journey::error_)
      
      // Return travel_time as int (minutes)
      .def("travel_time", [](journey const& j) {
        return j.travel_time().count();
      })
      .def("departure_time", [](journey const& j) {
        return j.departure_time().time_since_epoch().count();
      })
      .def("arrival_time", [](journey const& j) {
        return j.arrival_time().time_since_epoch().count();
      })
      .def("transport_legs", [](journey const& j) {
        return std::count_if(begin(j.legs_), end(j.legs_), [](journey::leg const& l) {
          return std::holds_alternative<journey::run_enter_exit>(l.uses_);
        });
      })
      .def("dominates", &journey::dominates)
      
      .def(py::self == py::self)
      .def(py::self < py::self)
      
      .def("__repr__", [](journey const& j) {
        return "Journey(legs=" + std::to_string(j.legs_.size()) +
               ", transfers=" + std::to_string(j.transfers_) +
               ", travel_time=" + std::to_string(j.travel_time().count()) + ")";
      })
      
      .def("__len__", [](journey const& j) { return j.legs_.size(); })
      .def("__getitem__", [](journey const& j, size_t i) -> journey::leg const& {
        if (i >= j.legs_.size()) throw py::index_error();
        return j.legs_[i];
      });

  // One-to-all output
  py::class_<fastest_offset>(m, "FastestOffset")
      .def(py::init<>())
      .def_readonly("duration", &fastest_offset::duration_)
      .def_readonly("transfers", &fastest_offset::k_)
      .def("has_connection", [](fastest_offset const& fo) {
        return fo.k_ != std::numeric_limits<std::uint8_t>::max();
      })
      .def("__repr__", [](fastest_offset const& fo) {
        auto const no_connection =
            fo.k_ == std::numeric_limits<std::uint8_t>::max();
        if (no_connection) {
          return std::string("FastestOffset(no_connection)");
        }
        return "FastestOffset(duration=" + std::to_string(fo.duration_) +
               ", transfers=" + std::to_string(fo.k_) + ")";
      });

  py::class_<raptor_state>(m, "RaptorState")
      .def(py::init<>())
      .def_property_readonly("n_locations",
                             [](raptor_state const& s) {
                               return s.n_locations_;
                             })
      .def("__repr__", [](raptor_state const& s) {
        return "RaptorState(n_locations=" + std::to_string(s.n_locations_) +
               ")";
      });

  // Routing functions  
  m.def("route",
        [](timetable const& tt, query q,
           direction dir) -> std::vector<journey> {
          search_state s_state;
          raptor_state r_state;
          auto const results =
              raptor_search(tt, nullptr, s_state, r_state, std::move(q), dir);
          if (results.journeys_ == nullptr) {
            return {};
          }
          return std::vector<journey>{results.journeys_->begin(), results.journeys_->end()};
        },
        py::arg("timetable"),
        py::arg("query"),
        py::arg("direction") = direction::kForward,
        "Execute routing query");

  m.def("route_with_rt",
        [](timetable const& tt, rt_timetable const* rtt, query q) 
        -> std::vector<journey> {
          search_state s_state;
          raptor_state r_state;
          auto const results = raptor_search(
              tt, rtt, s_state, r_state, std::move(q), direction::kForward);
          if (results.journeys_ == nullptr) {
            return {};
          }
          return std::vector<journey>{results.journeys_->begin(), results.journeys_->end()};
        },
        py::arg("timetable"),
        py::arg("rt_timetable"),
        py::arg("query"),
        "Execute routing query with real-time data");

  m.def("route_with_rt",
        [](timetable const& tt,
           rt_timetable const* rtt,
           query q,
           direction dir) -> std::vector<journey> {
          search_state s_state;
          raptor_state r_state;
          auto const results =
              raptor_search(tt, rtt, s_state, r_state, std::move(q), dir);
          if (results.journeys_ == nullptr) {
            return {};
          }
          return std::vector<journey>{results.journeys_->begin(),
                                      results.journeys_->end()};
        },
        py::arg("timetable"),
        py::arg("rt_timetable"),
        py::arg("query"),
        py::arg("direction"),
        "Execute routing query with real-time data and explicit direction");

  // One-to-all routing (returns internal raptor state)
  m.def("one_to_all",
        [](timetable const& tt,
           query q,
           direction dir,
           rt_timetable const* rtt) -> raptor_state {
          if (dir == direction::kForward) {
            return one_to_all<direction::kForward>(tt, rtt, q);
          }
          return one_to_all<direction::kBackward>(tt, rtt, q);
        },
        py::arg("timetable"),
        py::arg("query"),
        py::arg("direction") = direction::kForward,
        py::arg("rt_timetable") = nullptr,
        "Execute one-to-all routing and return search state");

  // Extract one-to-all result for a single location.
  m.def("one_to_all_fastest_offset",
        [](timetable const& tt,
           raptor_state const& state,
           direction dir,
           location_idx_t location,
           unixtime_t start_time,
           std::uint8_t max_transfers) {
          return get_fastest_one_to_all_offsets(
              tt, state, dir, location, start_time, max_transfers);
        },
        py::arg("timetable"),
        py::arg("state"),
        py::arg("direction"),
        py::arg("location"),
        py::arg("start_time"),
        py::arg("max_transfers") = kMaxTransfers,
        "Get fastest one-to-all result for one location");

  // Convenience helper that runs one-to-all and returns per-location summary.
  m.def("one_to_all_fastest_offsets",
        [](timetable const& tt,
           query q,
           direction dir,
           std::uint8_t max_transfers,
           rt_timetable const* rtt) {
          auto const start_time = get_one_to_all_start_time(q);
          auto state = dir == direction::kForward
                           ? one_to_all<direction::kForward>(tt, rtt, q)
                           : one_to_all<direction::kBackward>(tt, rtt, q);

          py::list out;
          for (auto i = 0U; i < tt.n_locations(); ++i) {
            auto const loc = location_idx_t{i};
            auto const fastest = get_fastest_one_to_all_offsets(
                tt, state, dir, loc, start_time, max_transfers);
            py::dict row;
            row["location"] = py::int_(loc.v_);
            row["duration"] = py::int_(fastest.duration_);
            row["transfers"] = py::int_(fastest.k_);
            row["has_connection"] =
                py::bool_(fastest.k_ != std::numeric_limits<std::uint8_t>::max());
            out.append(std::move(row));
          }
          return out;
        },
        py::arg("timetable"),
        py::arg("query"),
        py::arg("direction") = direction::kForward,
        py::arg("max_transfers") = kMaxTransfers,
        py::arg("rt_timetable") = nullptr,
        "Run one-to-all routing and return fastest offsets for all locations");
}
