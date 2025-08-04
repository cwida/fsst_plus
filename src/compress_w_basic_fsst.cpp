#include <duckdb.hpp>
#include <basic_fsst.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include "../global.h"
#include <ranges>
#include "fsst_plus.h"
#include <fstream>
#include <sstream>
#include <cstdio>

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path> <column_name> <amount_strings>" << std::endl;
        return 1;
    }

    std::string dataset_path = argv[1];
    std::string column_name = argv[2];
    std::string amount_strings = argv[3];

    try {
        // Connect to DuckDB in-memory database
        duckdb::DuckDB db(nullptr);
        duckdb::Connection con(db);
        
        //if dataset_path contains .csv, then use read_csv, otherwise use read_parquet
        string loader = "";
        bool cleanup_needed = false;
        std::string decompressed_path = "";

        if (dataset_path.substr(dataset_path.length() - 8) == ".csv.bz2") {
            // first decompress the file then read with duckdb
            decompressed_path = dataset_path.substr(0, dataset_path.length() - 4);
            std::string command = "bzip2 -dk " + dataset_path; // -k keeps the original file
            cleanup_needed = true;
            int result = system(command.c_str());
            if (result != 0) {
                std::cerr << "Error decompressing file: " << dataset_path << std::endl;
                return 1;
            }
            loader = "read_csv_auto('" + decompressed_path + "', header=true) ";
        } else if (dataset_path.substr(dataset_path.length() - 4) == ".csv") { 
            loader = "read_csv_auto('" + dataset_path + "', header=true) ";
        }else if (dataset_path.substr(dataset_path.length() - 8) == ".parquet") {
            loader = "read_parquet('" + dataset_path + "') ";
        } else {
            std::cerr << "Unsupported file type: " << dataset_path << std::endl;
            return 1;
        }
        const string query =
            "SELECT \"" + column_name + "\" FROM " + loader +
            " LIMIT " + amount_strings + ";";
        

        const auto result = con.Query(query);

        auto data_chunk = result->Fetch();
        if (!data_chunk || data_chunk->size() == 0) {
            std::cout << "No data for column: " << column_name << std::endl;
            throw std::runtime_error("No data for column: " + column_name);
        }

        const size_t n = std::min(config::amount_strings_per_symbol_table, static_cast<size_t>(result->RowCount()));
        
        StringCollection input = RetrieveData(result, data_chunk, n); // 100k rows
     
        
        // Run compression with basic FSST
        fsst_encoder_t encoder = CreateEncoder(input.lengths, input.string_ptrs);
        FSSTCompressionResult compression_result = FSSTCompress(input, &encoder);
        size_t total_compressed_size = 0;
        for (size_t i = 0; i < compression_result.encoded_string_lengths.size(); i++) {
            total_compressed_size += compression_result.encoded_string_lengths[i];
        }
        total_compressed_size += CalcSymbolTableSize(&encoder);
        

        // Add bitpacked offsets size
        size_t size_of_one_offset = 0; 
        if (input.lengths.size() > 0) { // Handle log2(0) case
            size_of_one_offset = ceil(log2(input.lengths.size()) / 8);
        }
        size_t total_offsets_size = input.lengths.size() * size_of_one_offset;
        total_compressed_size += total_offsets_size;
        printf("FSST_COMPRESSED_SIZE=%zu\n", total_compressed_size);
        if (cleanup_needed) {
            remove(decompressed_path.c_str());
        }
        return 0;
    } catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
} 