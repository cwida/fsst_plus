#pragma once
#include <cstddef>
#include <string>
#include <cstdint>

namespace config {
    // extern const size_t total_strings;
    constexpr size_t block_byte_capacity = UINT16_MAX;
    extern const bool print_sorted_corpus; // whether to print sorted strings.
    extern const bool print_split_points; // whether to print split points.
    extern const bool print_decompressed_corpus;

    constexpr size_t max_prefix_size = 255; // how far into the string to scan for a prefix. (max prefix size)
    constexpr size_t amount_strings_per_symbol_table = 120000; // 120000 = a duckdb row group
    size_t global_index = 0;
    // size_t block_granularity = 64;
    size_t block_granularity = 128;
    // size_t block_granularity = 192;
}
