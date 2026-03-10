#include "pybind_common.h"

#include "nigiri/types.h"
#include "nigiri/clasz.h"
#include "nigiri/stop.h"
#include "nigiri/footpath.h"

#include <chrono>
#include <cstdint>

namespace py = pybind11;
using namespace nigiri;

void init_types(py::module_& m) {
  // Basic type aliases
  py::class_<location_idx_t>(m, "LocationIdx")
      .def(py::init<>())
      .def(py::init<std::uint32_t>())
      .def("__int__", [](location_idx_t const& idx) { return idx.v_; })
      .def("__repr__", [](location_idx_t const& idx) {
        return "LocationIdx(" + std::to_string(idx.v_) + ")";
      })
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  py::class_<route_idx_t>(m, "RouteIdx")
      .def(py::init<>())
      .def(py::init<std::uint32_t>())
      .def("__int__", [](route_idx_t const& idx) { return idx.v_; })
      .def("__repr__", [](route_idx_t const& idx) {
        return "RouteIdx(" + std::to_string(idx.v_) + ")";
      })
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  py::class_<transport_idx_t>(m, "TransportIdx")
      .def(py::init<>())
      .def(py::init<std::uint32_t>())
      .def("__int__", [](transport_idx_t const& idx) { return idx.v_; })
      .def("__repr__", [](transport_idx_t const& idx) {
        return "TransportIdx(" + std::to_string(idx.v_) + ")";
      })
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  py::class_<trip_idx_t>(m, "TripIdx")
      .def(py::init<>())
      .def(py::init<std::uint32_t>())
      .def("__int__", [](trip_idx_t const& idx) { return idx.v_; })
      .def("__repr__", [](trip_idx_t const& idx) {
        return "TripIdx(" + std::to_string(idx.v_) + ")";
      })
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  py::class_<source_idx_t>(m, "SourceIdx")
      .def(py::init<>())
      .def(py::init<std::uint8_t>())
      .def("__int__", [](source_idx_t const& idx) { return idx.v_; })
      .def("__repr__", [](source_idx_t const& idx) {
        return "SourceIdx(" + std::to_string(idx.v_) + ")";
      })
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  // Duration and time types
  py::class_<duration_t>(m, "Duration")
      .def(py::init<>())
      .def(py::init<std::int16_t>())
      .def("count", [](duration_t const& d) { return d.count(); })
      .def("__int__", [](duration_t const& d) { return d.count(); })
      .def("__repr__", [](duration_t const& d) {
        return "Duration(" + std::to_string(d.count()) + " minutes)";
      })
      .def(py::self + py::self)
      .def(py::self - py::self)
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  py::class_<unixtime_t>(m, "UnixTime")
      .def(py::init<>())
      .def(py::init([](std::int64_t seconds) {
        return unixtime_t{std::chrono::duration<std::int64_t, std::ratio<60>>{seconds / 60}};
      }))
      .def("count", [](unixtime_t const& t) { return t.time_since_epoch().count(); })
      .def("__int__", [](unixtime_t const& t) { return t.time_since_epoch().count(); })
      .def("__repr__", [](unixtime_t const& t) {
        return "UnixTime(" + std::to_string(t.time_since_epoch().count()) + ")";
      })
      .def(py::self == py::self)
      .def(py::self != py::self)
      .def(py::self < py::self);

  // Enums
  py::enum_<clasz>(m, "Clasz")
      .value("AIR", clasz::kAir)
      .value("COACH", clasz::kCoach)
      .value("HIGHSPEED", clasz::kHighSpeed)
      .value("LONG_DISTANCE", clasz::kLongDistance)
      .value("NIGHT", clasz::kNight)
      .value("RIDE_SHARING", clasz::kRideSharing)
      .value("REGIONAL", clasz::kRegional)
      .value("SUBURBAN", clasz::kSuburban)
      .value("SUBWAY", clasz::kSubway)
      .value("TRAM", clasz::kTram)
      .value("BUS", clasz::kBus)
      .value("SHIP", clasz::kShip)
      .value("ODM", clasz::kODM)
      .value("FUNICULAR", clasz::kFunicular)
      .value("AERIAL_LIFT", clasz::kAerialLift)
      .value("OTHER", clasz::kOther)
      .export_values();

  py::enum_<location_type>(m, "LocationType")
      .value("GENERATED_TRACK", location_type::kGeneratedTrack)
      .value("TRACK", location_type::kTrack)
      .value("STATION", location_type::kStation)
      .export_values();

  py::enum_<event_type>(m, "EventType")
      .value("DEP", event_type::kDep)
      .value("ARR", event_type::kArr)
      .export_values();

  py::enum_<direction>(m, "Direction")
      .value("FORWARD", direction::kForward)
      .value("BACKWARD", direction::kBackward)
      .export_values();

  // Location ID
  py::class_<location_id>(m, "LocationId")
      .def(py::init<>())
      .def(py::init([](std::string const& id, source_idx_t src) {
        return location_id{std::string_view{id}, src};
      }),
           py::arg("id"),
           py::arg("src"))
      .def_readwrite("src", &location_id::src_)
      .def_readwrite("id", &location_id::id_)
      .def("__repr__", [](location_id const& lid) {
        return "LocationId(src=" + std::to_string(lid.src_.v_) + 
               ", id='" + std::string(lid.id_) + "')";
      });

  // Footpath
  py::class_<footpath>(m, "Footpath")
      .def(py::init<>())
      .def(py::init<location_idx_t, duration_t>())
      .def("target", &footpath::target)
      .def("duration", &footpath::duration)
      .def("__repr__", [](footpath const& fp) {
        return "Footpath(target=" + std::to_string(fp.target().v_) +
               ", duration=" + std::to_string(fp.duration().count()) + ")";
      });

  // Interval template
  py::class_<interval<unixtime_t>>(m, "TimeInterval")
      .def(py::init<>())
      .def(py::init<unixtime_t, unixtime_t>())
      .def_readwrite("from_", &interval<unixtime_t>::from_)
      .def_readwrite("to_", &interval<unixtime_t>::to_)
      .def("__repr__", [](interval<unixtime_t> const& i) {
        return "TimeInterval(from=" + std::to_string(i.from_.time_since_epoch().count()) +
               ", to=" + std::to_string(i.to_.time_since_epoch().count()) + ")";
      });
}
