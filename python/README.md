# PyNigiri API Guide

This guide explains how to work with `pynigiri` in practice first, and then gives a full reference of all exported classes, functions, enums, and important parameters.

## Overview: How This Is Used

Typical flow:

1. Load or read a `Timetable`.
2. Build a `Query` with start time and start/destination offsets.
3. Run either:
	 - one-to-one routing (`route`, `route_with_rt`) to get `Journey` objects, or
	 - one-to-all routing (`one_to_all`, `one_to_all_fastest_offset(s)`) for accessibility/catchment analysis.
4. Inspect `Journey` and `Leg` objects (for one-to-one) or `FastestOffset` rows (for one-to-all).

Core data model:

- `Timetable`: the transit graph and schedule data.
- `Query`: routing constraints and search settings.
- `Journey` -> `Leg`: one-to-one route results.
- `RaptorState`/`FastestOffset`: one-to-all intermediate and output data.

## Loading And Saving Timetables

There are two ways to get a timetable.

### Read prebuilt timetable

Use this when you already have a serialized timetable file/folder.

```python
import pynigiri as ng

tt = ng.read_timetable("/app/data/gtfs_nigirified")
print(tt.n_locations(), tt.n_routes(), tt.n_transports())
```

### Build from source files

Use this when loading from GTFS/NeTEx/etc input sources.

```python
import pynigiri as ng

sources = [ng.TimetableSource("my-feed", "/path/to/gtfs")]
opts = ng.FinalizeOptions()
tt = ng.load_timetable(sources, "2026-01-01", "2026-12-31", opts)
```

### Save timetable

```python
tt.write("/tmp/timetable.bin")
```

## Building A Query

`Query` controls what routing is allowed and what optimization bounds are applied.

```python
q = ng.Query()
```

### Timestamps

- `q.start_time` accepts:
	- a single integer minute timestamp (`int`), or
	- an interval tuple (`start_min, end_min`).
- For one-to-all, it must be a single timepoint.

Example:

```python
q.start_time = 29000000
# or q.start_time = (29000000, 29000120)
```

### Offsets

Offsets define start and destination anchors.

- `Offset(target, duration, transport_mode)`
- `target`: `LocationIdx`
- `duration`: minutes
- `transport_mode`: integer mode id (usually `TransportModeId(0)` for default)

What `Offset` means in practice:

- Think of it as an access/egress connector, not an in-vehicle trip.
- `target` is the network location where transit search attaches.
- `duration` is how long it takes to reach that location from the true origin (or from that location to the true destination).
- Multiple offsets let you model "start from any of these nearby stops" (or destination equivalents), each with different access times.
- In journey results, these show up as `leg.kind == "offset"` legs when relevant.

Example:

```python
start_loc = tt.find_location("49754")
dest_loc = tt.find_location("12345")

q.start = [ng.Offset(start_loc, 0, ng.TransportModeId(0))]
q.destination = [ng.Offset(dest_loc, 0, ng.TransportModeId(0))]
```

### Required fields (practical minimum)

For one-to-one:

- `start_time`
- `start`
- `destination`

For one-to-all:

- `start_time` (single int)
- `start`
- `via_stops` must stay empty

Useful tuning fields:

- `max_transfers`
- `max_travel_time`
- `start_match_mode`, `dest_match_mode`
- `allowed_claszes`
- `require_bike_transport`, `require_car_transport`

## Running One-to-One Routing

Standard query:

```python
journeys = ng.route(tt, q, ng.Direction.FORWARD)
```

With RT timetable:

```python
# rtt = ng.create_rt_timetable(tt, day)
journeys = ng.route_with_rt(tt, rtt, q, ng.Direction.FORWARD)
```

Inspect result:

```python
for j in journeys:
		print(j.travel_time(), j.transfers)
		for leg in j.legs:
				print(leg.kind, leg.dep_time, leg.arr_time)
				if leg.is_transport():
						print(leg.transport_info(tt))
```

## Running One-to-All Routing

One-to-all gives reachability/travel-time from one origin to all locations.

Why there are two steps (`one_to_all` first, then fastest offsets):

- `one_to_all(...)` runs the RAPTOR search once and returns `RaptorState`, which is a compact internal table of best times over rounds/transfers.
- `one_to_all_fastest_offset(...)` and `one_to_all_fastest_offsets(...)` are readout helpers on top of that state.
- This split is useful because you can reuse one computed state for many lookups without rerunning routing each time.

What "fastest offsets" mean:

- For a destination location, a `FastestOffset` is the best known travel result from the query start setup.
- `duration`: best travel time in minutes from start_time to that destination under query constraints.
- `transfers`: number of rounds used for that best result (effectively transfer depth indicator in this model).
- `has_connection()`: whether any valid connection was found within constraints.

### Minimal one-to-all

```python
q = ng.Query()
q.start_time = 29000000
q.start = [ng.Offset(tt.find_location("49754"), 0, ng.TransportModeId(0))]
q.max_transfers = 6
q.max_travel_time = 30

state = ng.one_to_all(tt, q, ng.Direction.FORWARD)
```

### Pull one destination result

```python
fo = ng.one_to_all_fastest_offset(
		tt,
		state,
		ng.Direction.FORWARD,
		ng.LocationIdx(207601),
		q.start_time,
		q.max_transfers,
)
print(fo.has_connection(), fo.duration, fo.transfers)
```

### Pull all destinations

```python
rows = ng.one_to_all_fastest_offsets(tt, q, ng.Direction.FORWARD, q.max_transfers)
reachable = [r for r in rows if r["has_connection"] and r["duration"] <= 30]
```

### Options and variations

- `direction`: `Direction.FORWARD` or `Direction.BACKWARD`
- `max_transfers`: search depth/transfer cap
- `max_travel_time`: upper time bound in minutes
- `rt_timetable`: optional RT-aware one-to-all

Hard constraints (native engine):

- `Query.start_time` must be a single time point (`int`)
- `Query.via_stops` must be empty

## API Reference

### Module Functions

Timetable:

- `read_timetable(path: str) -> Timetable`

Loader:

- `load_timetable(sources: list[TimetableSource], start_date: str, end_date: str, options: FinalizeOptions = FinalizeOptions()) -> Timetable`
- `load_timetable_dt(sources: list[TimetableSource], start, end, options: FinalizeOptions = FinalizeOptions()) -> Timetable`

Routing:

- `TransportModeId(id: int) -> int`
- `all_clasz_allowed()`
- `route(timetable: Timetable, query: Query, direction: Direction = Direction.FORWARD) -> list[Journey]`
- `route_with_rt(timetable: Timetable, rt_timetable: RtTimetable | None, query: Query) -> list[Journey]`
- `route_with_rt(timetable: Timetable, rt_timetable: RtTimetable | None, query: Query, direction: Direction) -> list[Journey]`

One-to-all:

- `one_to_all(timetable: Timetable, query: Query, direction: Direction = Direction.FORWARD, rt_timetable: RtTimetable | None = None) -> RaptorState`
- `one_to_all_fastest_offset(timetable: Timetable, state: RaptorState, direction: Direction, location: LocationIdx, start_time, max_transfers: int = 7) -> FastestOffset`
- `one_to_all_fastest_offsets(timetable: Timetable, query: Query, direction: Direction = Direction.FORWARD, max_transfers: int = 7, rt_timetable: RtTimetable | None = None) -> list[dict]`

Real-time:

- `create_rt_timetable(timetable: Timetable, day) -> RtTimetable`
- `gtfsrt_update_from_string(timetable: Timetable, rt_timetable: RtTimetable, source: SourceIdx, tag: str, data: str) -> Statistics`
- `gtfsrt_update_from_bytes(timetable: Timetable, rt_timetable: RtTimetable, source: SourceIdx, tag: str, data: bytes) -> Statistics`
- `gtfsrt_update_from_file(timetable: Timetable, rt_timetable: RtTimetable, source: SourceIdx, tag: str, file_path: str) -> Statistics`

### Enums

- `Clasz`: `AIR`, `COACH`, `HIGHSPEED`, `LONG_DISTANCE`, `NIGHT`, `REGIONAL`, `REGIONAL_FAST`, `SUBWAY`, `TRAM`, `BUS`, `SHIP`, `OTHER`
- `LocationType`: `GENERATED_TRACK`, `TRACK`, `STATION`
- `EventType`: `DEP`, `ARR`
- `Direction`: `FORWARD`, `BACKWARD`
- `LocationMatchMode`: `EXACT`, `ONLY_CHILDREN`, `EQUIVALENT`, `INTERMODAL`

### Classes And Types

Core wrappers:

- `LocationIdx()`, `LocationIdx(value: int)`
- `RouteIdx()`, `RouteIdx(value: int)`
- `TransportIdx()`, `TransportIdx(value: int)`
- `TripIdx()`, `TripIdx(value: int)`
- `SourceIdx()`, `SourceIdx(value: int)`
- `Duration()`, `Duration(value: int)`
- `UnixTime()`, `UnixTime(seconds: int)`

Other primitives:

- `LocationId()`, `LocationId(id: str, src: SourceIdx)`
- `Footpath()`, `Footpath(target: LocationIdx, duration: Duration)`
- `TimeInterval()`, `TimeInterval(from_: UnixTime, to_: UnixTime)`
- `LatLng()`, `LatLng(lat: float, lng: float)`

Timetable:

- `Timetable()`
- Methods:
	- `find_location(id: str, src: SourceIdx = SourceIdx(0)) -> LocationIdx | None`
	- `get_location_name(loc: LocationIdx) -> str`
	- `get_location_coords(loc: LocationIdx) -> LatLng`
	- `get_location_type(loc: LocationIdx) -> LocationType`
	- `get_location_parent(loc: LocationIdx) -> LocationIdx`
	- `n_locations() -> int`
	- `n_routes() -> int`
	- `n_transports() -> int`
	- `date_range() -> tuple[int, int]`
	- `write(path: str)`

Loader classes:

- `LoaderConfig()` fields: `link_stop_distance`, `default_tz`, `extend_calendar`
- `FinalizeOptions()` fields: `adjust_footpaths`, `merge_dupes_intra_src`, `merge_dupes_inter_src`, `max_footpath_length`
- `TimetableSource()`
- `TimetableSource(tag: str, path: str, config: LoaderConfig = LoaderConfig())`
- `TimetableSource` fields: `tag`, `path`, `loader_config`

Routing classes:

- `Offset(target: LocationIdx, duration: int, transport_mode: int = 0)`
	- Methods: `target()`, `duration()`, `type()`
- `TdOffset()` fields: `valid_from`, `duration`, `transport_mode_id`
- `ViaStop()` field: `location`; property: `stay`
- `TransferTimeSettings()` fields: `default`, `min_transfer_time`, `additional_time`, `factor`
- `Query()`
	- Property: `start_time` (`int` or `(int, int)` tuple)
	- Fields: `start_match_mode`, `dest_match_mode`, `use_start_footpaths`, `start`, `destination`, `max_start_offset`, `max_transfers`, `max_travel_time`, `min_connection_count`, `extend_interval_earlier`, `extend_interval_later`, `prf_idx`, `allowed_claszes`, `require_bike_transport`, `require_car_transport`, `transfer_time_settings`, `via_stops`, `slow_direct`
	- Method: `flip_dir()`

Journey model:

- `Leg`
	- Fields: `from`, `to`
	- Properties: `dep_time`, `arr_time`, `kind`
	- Methods: `duration()`, `is_transport()`, `is_footpath()`, `is_offset()`, `transport_info(timetable, rt_timetable=None)`, `footpath_info()`, `offset_info()`, `from_id(timetable)`, `to_id(timetable)`, `from_name(timetable)`, `to_name(timetable)`
- `Journey`
	- Fields: `legs`, `transfers`, `destination`, `error`
	- Properties: `start_time`, `dest_time`
	- Methods: `travel_time()`, `departure_time()`, `arrival_time()`, `transport_legs()`, `dominates(other)`

One-to-all types:

- `RaptorState()` property: `n_locations`
- `FastestOffset()` fields: `duration`, `transfers`; method: `has_connection()`

Real-time types:

- `RtTimetable()`
- `Statistics()` fields: `parser_error`, `no_header`, `total_entities`, `total_entities_success`, `total_entities_fail`, `total_alerts`, `total_vehicles`, `trip_update_without_trip`, `trip_resolve_error`, `unsupported_schedule_relationship`

