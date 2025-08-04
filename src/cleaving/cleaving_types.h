#pragma once
#include <vector>
#include <cstddef> // for size_t
#include <string>

struct SimilarityChunk {
    size_t start_index; // Starts here and goes on until next chunk's index, or until the end of the 128 block
    size_t prefix_length;
};

// Common base struct for Prefixes and Suffixes
struct StringCollection {
    std::vector<size_t> lengths;
    std::vector<const unsigned char *> string_ptrs;
    std::vector<std::string> data;  // retains the actual string bytes

    explicit StringCollection(const size_t n) {
        lengths.reserve(n);
        string_ptrs.reserve(n);
        data.reserve(n);
    }
};

struct Prefixes : StringCollection {
    explicit Prefixes(const size_t n) : StringCollection(n) {}
};

struct Suffixes : StringCollection {
    explicit Suffixes(const size_t n) : StringCollection(n) {}
};

struct CleavedResult {
    Prefixes prefixes;
    Suffixes suffixes;
    
    explicit CleavedResult(const size_t n): prefixes(Prefixes(n)), suffixes(Suffixes(n)){}
};