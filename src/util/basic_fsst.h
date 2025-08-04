#pragma once
#include <fsst.h>
#include <print_utils.h>
#include <string>
#include <cmath>
#include "duckdb.hpp"
#include "duckdb_utils.h"
#include <string>
#include <chrono>
#include "../global.h"
#include <generic_utils.h>

struct FSSTCompressionResult {
    fsst_encoder_t *encoder;
    std::vector<size_t> encoded_string_lengths;
    std::vector<unsigned char *> encoded_string_ptrs;
    unsigned char *output_buffer;
    size_t number_of_strings_compressed;
};
inline void PrintFSSTCompressionResult(const FSSTCompressionResult &fsst_compression_result) {
    PrintDecoderSymbolTable(fsst_decoder(fsst_compression_result.encoder));
    printf(">>>COMPRESSION RESULT<<<\n");
    // Print strings
    for (size_t i = 0; i < fsst_compression_result.encoded_string_lengths.size(); ++i) {
        std::cout << "i " << std::setw(3) << i << ": ";
        for (size_t j = 0; j < fsst_compression_result.encoded_string_lengths[i]; ++j) {
            std::cout << static_cast<int>(fsst_compression_result.encoded_string_ptrs[i][j]) << " ";
        }
        std::cout << std::endl;
    }
}

inline fsst_encoder_t *CreateEncoder(const std::vector<size_t> &lenIn, std::vector<const unsigned char *> &strIn) {
    constexpr int zeroTerminated = 0; // DuckDB strings are not zero-terminated
    fsst_encoder_t *encoder = fsst_create(
        lenIn.size(), /* IN: number of strings in batch to sample from. */
        lenIn.data(), /* IN: byte-lengths of the inputs */
        strIn.data(), /* IN: string start pointers. */
        zeroTerminated
        /* IN: whether input strings are zero-terminated. If so, encoded strings are as well (i.e. symbol[0]=""). */
    );
    return encoder;
}

inline size_t CalcSymbolTableSize(fsst_encoder_t *encoder) {
    size_t result = 0;
    // Correctly calculate decoder size by serialization
    unsigned char buffer[FSST_MAXHEADER];
    size_t serialized_size = fsst_export(encoder, buffer);
    result += serialized_size;

    return result;
}

inline void ExtractStringsFromResultChunk(const duckdb::unique_ptr<duckdb::DataChunk> &data_chunk,
                                              std::vector<std::string> &original_strings, std::vector<size_t> &lenIn,
                                              std::vector<const unsigned char *> &strIn) {

    auto &vector = data_chunk->data[0];
    auto vector_data = duckdb::FlatVector::GetData<duckdb::string_t>(vector);

    // Populate lenIn and strIn
    for (size_t i = 0; i < data_chunk->size(); i++) {

        const char *data_ptr = vector_data[i].GetData();

        if (!vector_data[i].Empty()) { //TODO: Should save "" instead right?
            std::size_t string_size = vector_data[i].GetSize();
            std::string str = vector_data[i].GetString(); // TODO: Throws segfault when value is null

            original_strings.push_back(str);
            const std::string &stored_str = original_strings.back();
            lenIn.push_back(stored_str.size());
            // strIn.push_back(reinterpret_cast<const unsigned char*>(str.c_str())); // This is not working, it just points to the single str value
            strIn.push_back(reinterpret_cast<const unsigned char *>(stored_str.c_str()));
        }
    }
};

inline void VerifyDecompressionCorrectness(const StringCollection &input, const std::vector<size_t> & encoded_string_lengths, const std::vector<unsigned char *> & encoded_string_ptrs, size_t number_of_strings_compressed,
                                           const fsst_decoder_t & decoder) {
    if (number_of_strings_compressed != input.data.size()) {
        throw std::logic_error("Basic FSST compressed size is not equal to input size ");
    }
    for (size_t i = 0; i < number_of_strings_compressed; i++) {
        // Allocate decompression buffer
        constexpr size_t BUFFER_SIZE = 1000000;
        unsigned char *result = new unsigned char[BUFFER_SIZE];
        
        size_t decompressed_size = fsst_decompress(
            &decoder, /* IN: use this symbol table for compression. */
            encoded_string_lengths[i],  /* IN: byte-length of compressed string. */
            encoded_string_ptrs[i], /* IN: compressed string. */
            BUFFER_SIZE, /* IN: byte-length of output buffer. */
            result /* OUT: memory buffer to put the decompressed string in. */
        );
        // const std::string_view decompressed_view(reinterpret_cast<char*>(decompressed), decompressed_size);

        // Verify decompression
        if (decompressed_size != input.lengths[i] ||
            !TextMatches(result, input.string_ptrs[i], input.lengths[i])) {
            std::cerr << "Decompression mismatch for string " << i <<"\n    Expected: "<< input.string_ptrs[i] <<"\n    Got:      " << result <<std::endl;
            
            // examine symbol table
            for (int j = 0; j < decompressed_size; ++j) {
                const unsigned long long symbol = decoder.symbol[*(encoded_string_ptrs[i] + j)];

                // Convert symbol to text and print it
                unsigned char symbolBytes[8];
                std::memcpy(symbolBytes, &symbol, sizeof(symbol));
                std::cerr << "Symbol " << j << ": [";
                for (int k = 0; k < decoder.len[*(encoded_string_ptrs[i] + j)]; ++k) {
                    // Print the byte both as integer and as character if printable
                    std::cerr << static_cast<int>(symbolBytes[k]);
                    if (symbolBytes[k] >= 32 && symbolBytes[k] <= 126) {
                        std::cerr << "(" << static_cast<char>(symbolBytes[k]) << ")";
                    }
                    if (k < decoder.len[*(encoded_string_ptrs[i] + j)] - 1) {
                        std::cerr << ", ";
                    }
                }
                std::cerr << "]" << std::endl;
            }
            // Free the result buffer to avoid memory leak
            delete[] result;
            throw std::logic_error("Decompression mismatch detected. Terminating.");
        }
        // Free the result buffer to avoid memory leak
        delete[] result;
    }
  
    std::cout << "Decompression verified\n";
};

inline FSSTCompressionResult FSSTCompress(StringCollection &input, fsst_encoder_t *encoder) {
    const size_t n = input.lengths.size();

    // Compression outputs
    std::vector<size_t> lenOut(n);
    std::vector<unsigned char *> strOut(n);

    // Calculate worst-case output size (2 * input) + datastructure overhead
    size_t max_out_size = 0;
    for (size_t i = 0; i < n; i++) {
        max_out_size += input.lengths[i];
    }
    max_out_size = max_out_size * 5; // *5 to cover datastructure overhead just in case
    unsigned char *output = static_cast<unsigned char *>(malloc(max_out_size));


    //////////////// COMPRESSION ////////////////
    size_t number_of_strings_compressed = fsst_compress(
        encoder, /* IN: encoder obtained from fsst_create(). */
        input.lengths.size(), /* IN: number of strings in batch to compress. */
        input.lengths.data(), /* IN: byte-lengths of the inputs */
        input.string_ptrs.data(), /* IN: input string start pointers. */
        max_out_size, /* IN: byte-length of output buffer. */
        output, /* OUT: memory buffer to put the compressed strings in (one after the other). */
        lenOut.data(), /* OUT: byte-lengths of the compressed strings. */
        strOut.data() /* OUT: output string start pointers. Will all point into [output,output+size). */
    );

    if (number_of_strings_compressed != n) {
        // See if all size is zero
        size_t total_size = 0;
        for (size_t i = 0; i < input.lengths.size(); i++) {
            total_size += input.lengths[i];
        }
        if (total_size != 0) {
            throw std::logic_error("Number of compressed strings doesn't match number of strings of input. Compressed " + std::to_string(number_of_strings_compressed) + " strings, expected " + std::to_string(n) + " strings.\n");
        } else {
            printf("All strings are empty. Compressed %zu strings, input had %zu strings. \n", number_of_strings_compressed, n);
        }
    }
    // print_compressed_strings(input.lengths, strIn, lenOut, strOut, num_compressed);

    // fsst_decoder_t decoder = fsst_decoder(encoder);
    // print_decoder_symbol_table(decoder);

    return FSSTCompressionResult{encoder, lenOut, strOut, output, number_of_strings_compressed};
}

// Declaration for the function that runs basic FSST compression and prints its results, using the provided DuckDB connection, parquet file path, and limit.
inline void RunBasicFSST(duckdb::Connection &con, StringCollection &input, const size_t &total_string_size, Metadata &metadata, double extra_compressed_size = 0.0) {

    metadata.amount_of_rows = input.data.size();
    
    size_t total_strings_amount = {0};
    size_t total_compressed_string_size = {0};

    const auto start_time = std::chrono::high_resolution_clock::now();

    // Create FSST encoder
    fsst_encoder_t *encoder = CreateEncoder(input.lengths, input.string_ptrs);

    /* =============================================
     * ================ COMPRESSION ================
     * ===========================================*/

    FSSTCompressionResult compression_result = FSSTCompress(input, encoder);


    auto end_time = std::chrono::high_resolution_clock::now();


    /* =============================================
     * =============== DECOMPRESSION ===============
     * ===========================================*/

    // fsst_decoder_t decoder = fsst_decoder(encoder);
    VerifyDecompressionCorrectness(input, compression_result.encoded_string_lengths, compression_result.encoded_string_ptrs, compression_result.number_of_strings_compressed, fsst_decoder(encoder));

    for (size_t compressed_string_length: compression_result.encoded_string_lengths) {
        total_compressed_string_size += compressed_string_length;
    }
    total_compressed_string_size += CalcSymbolTableSize(encoder);
    total_strings_amount += input.lengths.size();

    // Add bitpacked offsets size
    size_t size_of_one_offset = 0; 
    if (total_strings_amount > 0) { // Handle log2(0) case
        size_of_one_offset = ceil(log2(total_strings_amount) / 8);
    }
    size_t total_offsets_size = total_strings_amount * size_of_one_offset;
    total_compressed_string_size += total_offsets_size;
    total_compressed_string_size += extra_compressed_size;

    // Free FSST encoder and output buffer for this batch
    fsst_destroy(encoder);
    free(compression_result.output_buffer);

    
    metadata.run_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    metadata.compression_factor = static_cast<double>(total_string_size) / static_cast<double>(total_compressed_string_size);
    
    PrintCompressionStats(total_strings_amount, total_string_size, total_compressed_string_size, metadata);
    
    // Store results in the database
    std::string insert_query = "INSERT INTO results VALUES ('" +
                               metadata.dataset_folders + "', '" +
                               metadata.dataset + "', '" +
                               metadata.column + "', '" +
                               metadata.algo + "', " +
                               std::to_string(metadata.amount_of_rows) + ", " +
                               std::to_string(metadata.run_time_ms) + ", " +
                               std::to_string(metadata.compression_factor) + ", " +
                               std::to_string(total_strings_amount) + ", " +
                               std::to_string(total_string_size) + ");";
        
    try {
        con.Query(insert_query);
        std::cout << "Inserted result for Basic FSST on " << metadata.dataset << "." << metadata.column << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Failed to insert Basic FSST result: " << e.what() << std::endl;
    }
}

inline size_t CalcEncodedStringsSize(const StringCollection &encoded_strings) {
    size_t result = 0;
    const size_t size = encoded_strings.lengths.size();
    for (size_t i = 0; i < size; ++i) {
        result += encoded_strings.lengths[i];
    }
    return result;
}
