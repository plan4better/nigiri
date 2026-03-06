#include "pybind_common.h"

#include "nigiri/string_store.h"
#include "nigiri/timetable.h"

#include "geo/latlng.h"

#include <optional>
#include <string>
#include <string_view>

namespace py = pybind11;
using namespace nigiri;

void init_timetable(py::module_& m) {
  // geo::latlng
  py::class_<geo::latlng>(m, "LatLng")
      .def(py::init<>())
      .def(py::init<double, double>())
      .def_readwrite("lat", &geo::latlng::lat_)
      .def_readwrite("lng", &geo::latlng::lng_)
      .def("__repr__",
           [](geo::latlng const& ll) {
             return "LatLng(lat=" + std::to_string(ll.lat_) +
                    ", lng=" + std::to_string(ll.lng_) + ")";
           })
      .def(py::self == py::self);

  // Timetable - simplified binding focusing on key functionality
  py::class_<timetable>(m, "Timetable")
      .def(py::init<>())

      // Location queries
      .def(
          "find_location",
          [](timetable const& tt, std::string const& id,
             source_idx_t src) -> std::optional<location_idx_t> {
            return tt.find(location_id{id, src});
          },
          py::arg("id"), py::arg("src") = source_idx_t{0},
          "Find location by ID")

      .def(
          "get_location_name",
          [](timetable const& tt, location_idx_t const loc) -> std::string {
            return std::string(tt.get_default_name(loc));
          },
          "Get location name")

      .def(
          "get_location_coords",
          [](timetable const& tt, location_idx_t const loc) -> geo::latlng {
            return tt.locations_.coordinates_[loc];
          },
          "Get location coordinates")

      .def(
          "get_location_type",
          [](timetable const& tt, location_idx_t const loc) -> location_type {
            return tt.locations_.types_[loc];
          },
          "Get location type")

      .def(
          "get_location_parent",
          [](timetable const& tt, location_idx_t const loc) -> location_idx_t {
            return tt.locations_.parents_[loc];
          },
          "Get location parent")

      .def(
          "n_locations",
          [](timetable const& tt) { return tt.locations_.coordinates_.size(); },
          "Get number of locations")

      .def(
          "n_routes",
          [](timetable const& tt) { return tt.route_transport_ranges_.size(); },
          "Get number of routes")

      .def(
          "n_transports",
          [](timetable const& tt) { return tt.transport_route_.size(); },
          "Get number of transports")

      // Date range - returns (start_day, end_day) as integers (days since
      // epoch)
      .def(
          "date_range",
          [](timetable const& tt) {
            auto r = tt.internal_interval_days();
            return py::make_tuple(r.from_.time_since_epoch().count(),
                                  r.to_.time_since_epoch().count());
          },
          "Get timetable date range as (start_day, end_day) in days since "
          "epoch")

      .def(
          "write",
          [](timetable const& tt, std::string const& path) { tt.write(path); },
          py::arg("path"), "Write timetable to a binary file")

      .def("__repr__", [](timetable const& tt) {
        return "Timetable(locations=" +
               std::to_string(tt.locations_.coordinates_.size()) +
               ", routes=" + std::to_string(tt.route_transport_ranges_.size()) +
               ", transports=" + std::to_string(tt.transport_route_.size()) +
               ")";
      });

  m.def(
      "read_timetable",
      [](std::string const& path) {
        auto tt = *timetable::read(path);
        tt.resolve();
        return tt;
      },
      py::arg("path"), "Read timetable from a binary file");

  // Transport info
  py::class_<timetable::transport>(m, "Transport")
      .def_readonly("bitfield_idx", &timetable::transport::bitfield_idx_)
      .def_readonly("route_idx", &timetable::transport::route_idx_)
      .def("__repr__", [](timetable::transport const& t) {
        return "Transport(route=" + std::to_string(t.route_idx_.v_) + ")";
      });
}
