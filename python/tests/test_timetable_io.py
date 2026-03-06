"""
Unit tests for timetable persistence APIs.
"""
import pytest
import pynigiri as ng


def test_timetable_write_read_roundtrip(tmp_path):
    """Timetable can be written to and read from disk."""
    tt = ng.Timetable()
    out = tmp_path / "tt.bin"

    tt.write(str(out))
    assert out.exists()

    loaded = ng.read_timetable(str(out))
    assert loaded is not None
    assert loaded.n_locations() == tt.n_locations()
    assert loaded.n_routes() == tt.n_routes()
    assert loaded.n_transports() == tt.n_transports()


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
