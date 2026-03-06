import pynigiri as ng
from datetime import datetime

# 1) Read timetable
tt = ng.read_timetable("/app/data/gtfs_nigirified")

# 2) Build one-to-all query from parent station stop_id 49754
q = ng.Query()
query_time = datetime(2026, 3, 6, 11, 0, 0)
q.start_time = int(query_time.timestamp()) // 60
start_loc = tt.find_location("49754")
if start_loc is None:
    raise RuntimeError("Could not resolve stop_id 49754 in timetable")
q.start = [ng.Offset(start_loc, 0, ng.TransportModeId(0))]
q.max_transfers = 1
q.max_travel_time = 10
q.start_match_mode = ng.LocationMatchMode.EQUIVALENT

# 3) Run one-to-all
rows = ng.one_to_all_fastest_offsets(tt, q, ng.Direction.FORWARD, q.max_transfers)

# 4) Keep reachable within 30 min and convert to names
reachable = [r for r in rows if r["has_connection"]]
names = []
for r in reachable:
    loc_idx = ng.LocationIdx(int(r["location"]))
    names.append(tt.get_location_name(loc_idx))

# optional dedupe
names = list(dict.fromkeys(names))
print(names)