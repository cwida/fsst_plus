#pragma once
#include <block_sizer.h>
#include <vector>
#include "basic_fsst.h"
#include "block_types.h"
#include <cmath>
struct FSSTPlusCompressionResult {
    fsst_encoder_t *encoder;
    uint8_t *data_start;
    uint8_t *data_end;
};

struct FSSTPlusSizingResult {
    std::vector<BlockWritingMetadata> wms;
    std::vector<size_t> block_sizes_pfx_summed;
};

inline size_t CalcMaxFSSTPlusDataSize(const CleavedResult &cleaved_result) {
    size_t result = {0};

    const size_t ns = cleaved_result.suffixes.string_ptrs.size(); // number of strings
    const size_t nb = ceil(static_cast<double>(ns) / config::block_granularity); // number of blocks
    const size_t all_blocks_overhead = nb * (1 + 1 + config::block_granularity * 2); // block header3
    result += all_blocks_overhead;
    result += CalcEncodedStringsSize(cleaved_result.prefixes);
    result += CalcEncodedStringsSize(cleaved_result.suffixes);
    result += ns * 3;
    // Add extra safety padding to avoid potential buffer overflows
    result += (nb * 1024); // 1KB extra per blockfor safety TODO: No real reason this should be done but was failing without
    return result;
}

inline StringCollection RetrieveData(const unique_ptr<MaterializedQueryResult> &result, unique_ptr<DataChunk> &data_chunk, const size_t &n) {
    // std::cout << "🔷 " << n << " strings for this symbol table 🔷 \n";

    StringCollection input(n);

    // Use input.data to store actual string contents, ensuring ownership persists
    while (data_chunk) {
        // Populate input.data, input.lengths, and input.strings
        ExtractStringsFromDataChunk(data_chunk, input.data, input.lengths, input.string_ptrs);

        data_chunk = result->Fetch();
    }

    return input;
}

inline FSSTPlusSizingResult SizeEverything(const size_t &n, const std::vector<SimilarityChunk> &similarity_chunks, const CleavedResult &cleaved_result) {
    // First calculate total size of all blocks
    std::vector<BlockWritingMetadata> wms;
    std::vector<size_t> block_sizes_pfx_summed;

    size_t suffix_area_start_index = 0; // start index for this block into all suffixes (stored in suffix_compression_result)

    while (suffix_area_start_index < n) {
        // Create fresh metadata for each block
        BlockWritingMetadata wm(config::block_granularity);  // Instead of reusing previous metadata
        wm.suffix_area_start_index = suffix_area_start_index;

        size_t block_size = CalculateBlockSizeAndPopulateWritingMetadata(
            similarity_chunks, cleaved_result, wm,
            suffix_area_start_index);
        size_t prefix_summed = block_sizes_pfx_summed.empty()
                                   ? block_size
                                   : block_sizes_pfx_summed.back() + block_size;
        block_sizes_pfx_summed.push_back(prefix_summed);
        wms.push_back(wm);

        suffix_area_start_index += wm.number_of_suffixes;
    }
    // std::cout << "We have this many blocks: " << wms.size() << "\n";
    return FSSTPlusSizingResult{wms, block_sizes_pfx_summed};
};

inline void RunDictionaryCompression(duckdb::Connection &con, const string &column_name, const string &dataset_path, const size_t &n, const size_t &total_string_size, Metadata &metadata) {
    // Quote the column name to handle spaces and special characters correctly in the SQL query.
    const string quoted_column_name = "\"" + column_name + "\"";
    // const string query = "SELECT length(string_agg(DISTINCT " + quoted_column_name + ")) as dict_size, COUNT(DISTINCT " + quoted_column_name + ") as dist, ceil(log2(dist) / 8) as size_of_code, COUNT(" + quoted_column_name + ") * size_of_code as codes_size, CAST(dict_size + codes_size as BIGINT)  as total_compressed_size FROM read_parquet('"+dataset_path+"');";
    const string query = "SELECT length(string_agg(DISTINCT " + quoted_column_name + ", '')) AS dict_size, COUNT(DISTINCT " + quoted_column_name + ") AS dist, CASE when dist = 0 then 0 else ceil(log2(dist) / 8) END AS size_of_code, COUNT(" + quoted_column_name + ") * size_of_code AS codes_size, CAST(dict_size + codes_size AS INT) AS total_compressed_size, format_bytes(total_compressed_size) AS formatted, length(string_agg(" + quoted_column_name + ", '')) AS raw_size, FROM (FROM read_parquet('"+dataset_path+"') LIMIT "+std::to_string(n)+");";
    
    const auto result = con.Query(query);

    auto chunk = result->Fetch();
    auto &vector = chunk->data[4];
    const uint32_t *total_compressed_size = FlatVector::GetData<uint32_t>(vector);
    double compression_factor = static_cast<double>(total_string_size) / static_cast<double>(*total_compressed_size);
    PrintCompressionStats(n, total_string_size, *total_compressed_size, metadata);

    // Store results in the database
    std::string insert_query = "INSERT INTO results VALUES ('" +
                               metadata.dataset_folders + "', '" +
                               metadata.dataset + "', '" +
                               metadata.column + "', '" +
                               metadata.algo + "', " +
                               std::to_string(metadata.amount_of_rows) + ", " +
                               std::to_string(0) + ", " +
                               std::to_string(compression_factor) + ", " +
                               std::to_string(n) + ", " +
                               std::to_string(total_string_size) + ");";

    try {
        con.Query(insert_query);
        std::cout << "Inserted result for DICTIONARY " << metadata.dataset << "." << column_name << std::endl;
    } catch (std::exception& e) {
        std::cerr << "🚨 Failed to insert result: " << e.what() << std::endl;
    }

};
