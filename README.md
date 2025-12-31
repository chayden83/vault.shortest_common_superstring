<!-- SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception -->

# Benchmarks

All benchmarks reported in the tables below are implemented using
Benchmark](https://github.com/google/benchmark) and executed on a quad
core laptop with 16GB DDR4 memory. Benchmark source code is available
in the `benchmarks` directory. Here is the CPU info reported by
`lscpu` on the benchmark machine.

```
Architecture:             x86_64
  CPU op-mode(s):         32-bit, 64-bit
  Address sizes:          39 bits physical, 48 bits virtual
  Byte Order:             Little Endian
CPU(s):                   4
  On-line CPU(s) list:    0-3
Vendor ID:                GenuineIntel
  Model name:             Intel(R) Core(TM) i5-6300HQ CPU @ 2.30GHz
    CPU family:           6
    Model:                94
    Thread(s) per core:   1
    Core(s) per socket:   4
    Socket(s):            1
    Stepping:             3
    CPU(s) scaling MHz:   81%
    CPU max MHz:          3200.0000
    CPU min MHz:          800.0000
```

| Benchmark                                               |        Time |         CPU | Iterations |
|:--------------------------------------------------------|------------:|------------:|-----------:|
| `string_set_intersection/8/manual_time`                 |      888 ns |      931 ns |     793984 |
| `string_set_intersection/64/manual_time`                |    11745 ns |    11794 ns |      56384 |
| `string_set_intersection/512/manual_time`               |   201519 ns |   201710 ns |       3513 |
| `string_set_intersection/4096/manual_time`              |  1903536 ns |  1904532 ns |        342 |
| `string_set_intersection/32768/manual_time`             | 16442827 ns | 16449666 ns |         43 |
| `string_set_intersection/262144/manual_time`            | 88982622 ns | 89055995 ns |          8 |
| `string_histogram_intersection/8/manual_time`           |     1083 ns |     1128 ns |     657344 |
| `string_histogram_intersection/64/manual_time`          |     7920 ns |     7969 ns |      88402 |
| `string_histogram_intersection/512/manual_time`         |    57795 ns |    63317 ns |      12227 |
| `string_histogram_intersection/4096/manual_time`        |   481430 ns |   519989 ns |       1453 |
| `string_histogram_intersection/32768/manual_time`       |  3639165 ns |  3650157 ns |        188 |
| `string_histogram_intersection/262144/manual_time`      | 18329224 ns | 18478572 ns |         37 |
| `string_view_histogram_intersection/8/manual_time`      |     1072 ns |     1116 ns |     647579 |
| `string_view_histogram_intersection/64/manual_time`     |     7786 ns |     7836 ns |      90730 |
| `string_view_histogram_intersection/512/manual_time`    |    60399 ns |    66369 ns |      11374 |
| `string_view_histogram_intersection/4096/manual_time`   |   483216 ns |   522250 ns |       1452 |
| `string_view_histogram_intersection/32768/manual_time`  |  3653163 ns |  3664133 ns |        190 |
| `string_view_histogram_intersection/262144/manual_time` | 18647274 ns | 18788655 ns |         38 |
