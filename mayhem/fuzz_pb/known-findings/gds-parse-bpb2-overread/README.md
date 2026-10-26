3-byte input found by `fuzz_pb` (selector byte 0x3d = blob parameter block): a BPB that ends right
after a tag, `01 0a`. `gds__parse_bpb2` (src/yvalve/gds.cpp:2076) reads the length byte after
`*p++` without checking `p < end`, a heap over-read reached from `isc_create_blob2`.

Reproduce: `/mayhem/fuzz_pb-standalone mayhem/fuzz_pb/known-findings/gds-parse-bpb2-overread/input.pb`
