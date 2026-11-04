5-byte request BLR found by `fuzz_blr` within seconds. `PAR_parse_node` returns NULL for a
non-value token and `PAR_parse_value` (src/jrd/par.cpp:1605) dereferences it:
`blr_count` (CountAggNode::parse, AggNodes.cpp:2861) followed by a byte that is not a value node.

Reproduce: `/mayhem/fuzz_blr-standalone mayhem/fuzz_blr/known-findings/par-parse-value-null-node/input.blr`
