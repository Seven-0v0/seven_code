# aux-eye fixtures

- `mug.jpg` — bright everyday-object frame (ground-truth `visible=true`).
- `dark.jpg` — pure-black frame (ground-truth `visible=false`); doubles as a marker-free frame for the ArUco graceful-failure test.
- `aruco-45deg.jpg` — real synthetic golden image with a DICT_4X4_50 marker (id 23) at a mathematically-imposed 45° yaw (marker ~19% of frame width). Regenerate with `python3 tools/gen_aruco_golden.py`. Asserted by `tests/aux-eye/test_aruco.py` (measured yaw ∈ [43, 47]).
