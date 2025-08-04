from pathlib import Path, PurePath
import os
import math
import duckdb
import tempfile
import subprocess
import traceback
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime

def refine_dataset(input_path: PurePath, output_path: PurePath):
    PROJECT_PATH = "YOUR_PROJECT_PATH/fsst-plus"
    print(f"\n\n ⚗️ ⚗️  Refining {str(input_path)} -> {str(output_path)} ⚗️ ⚗️")
    
    """Process a single data file using the refinement logic"""
    # Determine file type and read accordingly
    file_ext = input_path.suffix.lower()
    
    # Create temporary DuckDB database for dictionary size calculation
    con = duckdb.connect()
    
    try:
        relation = None;
        if file_ext == '.parquet':
            relation = con.read_parquet(str(input_path));
        elif file_ext == '.csv':
            relation = con.from_csv_auto(str(input_path), header=True, ignore_errors=True);
        else:
            print(f"⚠️ Unsupported file format: {file_ext}")
            return False
        
        if relation is None:
            print(f"⚠️ Failed to read DataFrame from {str(input_path)} (format: {file_ext}). Skipping refinement.")
            return False
        
        # Text column selection criteria
        columns = relation.columns
        types = relation.types # DuckDB data types
        col_types_map = dict(zip(columns, types))
        
        relation.limit(100000) #TODO: THIS DOES NOT REASSIGN THE RELATION OBJECT, SHOULD BE relation = relation.limit(100000)
        
        # Get total row count once
        count_result = relation.aggregate('count(*)').fetchone()
        if count_result is None:
            raise ValueError("Could not get row count from relation.")
        total_rows = count_result[0]
        if total_rows == 0:
            print("Relation is empty, no columns to filter.")
            return False

        text_columns = []
        for col in columns:
            col_type = col_types_map[col]
            col_type_str = str(col_type).upper()

            # non_null_count_result = relation.aggregate(f'count("{col}")').fetchone()
            # non_null_count = non_null_count_result[0]
            
            avg_len_query = f'avg(length(CAST("{col}" AS VARCHAR)))'
            avg_len_result = relation.aggregate(avg_len_query).fetchone()
            avg_len = avg_len_result[0] if avg_len_result and avg_len_result[0] is not None else 0
            
            if (col_type_str != 'VARCHAR' or # Not a string column
                # non_null_count < math.ceil(total_rows / 2.0) or # More than 50% non-null
                avg_len <= 8):
                print(f"Skipping column '{col}")
                continue
            
            if input_path.name == "clickbench.parquet":
                text_columns.append(col)
                print(f"Adding clickbench column '{col}'. Skipping dictionary size calculation.")
                continue
            
            # col_relation = relation.select(f'"{col}" AS THISCOL')
            # con.execute("CREATE OR REPLACE TABLE dictionary_sizer_table AS SELECT * FROM col_relation")
            # dict_query = """
            # SELECT length(string_agg(DISTINCT THISCOL)) as dict_size,  -- Size of dictionary (bytes needed to store all unique strings)
            #         COUNT(DISTINCT THISCOL) as dist,  -- Number of distinct values
            #         CASE WHEN COUNT(DISTINCT THISCOL) = 0 THEN 0 
            #              ELSE ceil(log2(COUNT(DISTINCT THISCOL)) / 8) 
            #         END as size_of_code,  -- Bytes needed per dictionary reference (log2 of cardinality)
                    
            #         CASE WHEN COUNT(DISTINCT THISCOL) = 0 THEN 0 
            #              ELSE COUNT(THISCOL) * ceil(log2(COUNT(DISTINCT THISCOL)) / 8) 
            #         END as codes_size,  -- Total size of all dictionary references
                    
            #         CASE WHEN COUNT(DISTINCT THISCOL) = 0 
            #             THEN CAST(length(string_agg(DISTINCT THISCOL)) as BIGINT) 
            #             ELSE CAST(length(string_agg(DISTINCT THISCOL)) + COUNT(THISCOL) * ceil(log2(COUNT(DISTINCT THISCOL)) / 8) as BIGINT) 
            #         END as total_compressed_size,  -- Dictionary size + references size
                    
            #         format_bytes(CASE WHEN COUNT(DISTINCT THISCOL) = 0 
            #             THEN CAST(length(string_agg(DISTINCT THISCOL)) as BIGINT) 
            #             ELSE CAST(length(string_agg(DISTINCT THISCOL)) + COUNT(THISCOL) * ceil(log2(COUNT(DISTINCT THISCOL)) / 8) as BIGINT) 
            #         END) as formatted  -- Human-readable format of total compressed size
            # FROM dictionary_sizer_table
            # """
            dict_query = 'SELECT length(string_agg(DISTINCT "' + col + '", \'\')) AS dict_size, COUNT(DISTINCT "' + col + '") AS dist, CASE WHEN COUNT(DISTINCT "' + col + '") = 0 THEN 0 ELSE ceil(log2(COUNT(DISTINCT "' + col + '")) / 8) END AS size_of_code, COUNT("' + col + '") * CASE WHEN COUNT(DISTINCT "' + col + '") = 0 THEN 0 ELSE ceil(log2(COUNT(DISTINCT "' + col + '")) / 8) END AS codes_size, CAST(length(string_agg(DISTINCT "' + col + '", \'\')) + COUNT("' + col + '") * CASE WHEN COUNT(DISTINCT "' + col + '") = 0 THEN 0 ELSE ceil(log2(COUNT(DISTINCT "' + col + '")) / 8) END AS INT) AS total_compressed_size, format_bytes(total_compressed_size) AS formatted, length(string_agg("' + col + '", \'\')) AS raw_size FROM (SELECT * FROM relation LIMIT ' + str(100000) + ')'
            # print(f"Running query '{dict_query}'")
            dict_result = con.execute(dict_query).fetchall()[0]
            dict_size = dict_result[4]  # total_compressed_size
            if dict_size is None:
                print(f"⚠️ No dictionary size for column '{col}'. Skipping.")
                continue
            
            # Run FSST compression using our C++ program
            print(f"Running FSST compression on column '{col}'")
            cmd = [f"{PROJECT_PATH}/build/compress_w_basic_fsst", str(input_path), col, "100000"]
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            # Parse FSST compressed size from output
            fsst_size = None
            if result.stdout is not None:
                for line in result.stdout.split('\n'):
                    if line.startswith("FSST_COMPRESSED_SIZE="):
                        fsst_size = int(line.split('=')[1])
                        break
            else:
                print(f"‼️‼️ No stdout from FSST compression for column '{col}'. Error: {result.stderr}")
            
            # Compare sizes and decide whether to include the column
            if fsst_size is not None:
                if fsst_size <= dict_size * 2:
                    text_columns.append(col)
                    print(f"👍 Column '{col}' passes filter: FSST size ({fsst_size}) is not 2x bigger than dictionary size ({dict_size}). Including.")
                else:
                    print(f"👎 Column '{col}' filtered out: FSST size ({fsst_size}) is 2x bigger than dictionary size ({dict_size}). Excluding.")
            else:
                print(f"🚨⏭️ ERROR, SKIPPING. Could not determine FSST size for column '{col}'. Output was:\n {result.stderr}")
                continue
                raise Exception(f"‼️‼️ Could not determine FSST size for column '{col}'. Output was:\n {result.stderr}")
        
        # Only write the file if there are text columns that meet our criteria
        if text_columns:
            # Ensure the parent directory exists
            output_path.parent.mkdir(parents=True, exist_ok=True)

            # Construct comma-separated string for column selection
            # Quote column names to handle potential special characters
            select_expr = ", ".join([f'\"{col}\"' for col in text_columns])
            
            # Select the final columns using the explicit expression
            refined_data = relation.select(select_expr)
            
            refined_data.write_parquet(str(output_path))
            print(f"✅ Processed {str(input_path)} -> {str(output_path)}, with columns: {text_columns}")
            return True
        else:
            print(f"⏭️ No columns found in {str(input_path)} that match the criteria. Skipping file.")
            return False
      
    finally:
        con.close()
        

def process_raw_directory(raw_dir: str = "../../data/raw", output_dir: str = "../../data/refined"):
    """Process all supported data files in the raw directory and its subdirectories using multiple threads"""
    raw_path = Path(raw_dir)
    output_path = Path(output_dir)

    # Create output directory if it doesn't exist
    output_path.mkdir(parents=True, exist_ok=True)

    # Find all matching files with supported extensions
    supported_files = []
    supported_files.extend(raw_path.glob("**/*.parquet"))
    supported_files.extend(raw_path.glob("**/*.csv"))
    # supported_files.extend(raw_path.glob("**/*.json"))

    processed_count = 0
    error_count = 0
    files_to_process = []

    # Prepare list of files to process, skipping already existing ones
    for data_file in supported_files:
        relative_path = data_file.relative_to(raw_path)
        output_file = output_path / relative_path.parent / (relative_path.stem.replace('.csv', '') + '.parquet')
        
        if output_file.exists():
            print(f"✅ Already refined {str(data_file)}, file already exists at {str(output_file)}")
            continue
        files_to_process.append((data_file, output_file))

    # Define a helper function for parallel processing
    def process_single_file(file_paths):
        data_file, output_file = file_paths
        try:
            # refine_dataset returns True if columns were found and file processed, False otherwise
            return refine_dataset(data_file, output_file)
        except Exception as e:
            print(f"🚨🚨 UNHANDLED ERROR processing {data_file}: {e}")
            print(f"Traceback:\n{traceback.format_exc()}")
            return False # Count as an error if an unhandled exception occurs

    # Determine the number of worker threads (adjust as needed, None uses os.cpu_count())
    # Consider I/O bound nature; more threads might be beneficial
    max_workers = os.cpu_count() * 2 # Example: Use double the CPU cores
    
    print(f"🚀 Starting parallel processing with up to {max_workers} threads for {len(files_to_process)} files...")

    results = []
    # Use ThreadPoolExecutor to process files in parallel
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        # Submit all tasks
        future_to_file = {executor.submit(process_single_file, file_pair): file_pair for file_pair in files_to_process}
        
        # Process results as they complete
        for i, future in enumerate(as_completed(future_to_file)):
            file_pair = future_to_file[future]
            data_file, _ = file_pair
            try:
                result = future.result() # result is True (success) or False (no columns/error)
                results.append(result)
                print(f"🏁 Completed {i + 1}/{len(files_to_process)}: {data_file} -> {'Success' if result else 'Skipped'}")
            except Exception as exc:
                print(f"‼️ Exception for {data_file}: {exc}")
                results.append(False) # Count as error on exception

    # Aggregate results
    processed_count = sum(results) # Sum of True values
    skipped_count = len(files_to_process) - processed_count

    print(f"🌐 Macrodata Refinement Complete 🌐")
    print(f"🕒 Current time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"Processed {len(files_to_process)} files:")
    print(f"  ✅ {processed_count} files matched the criteria and were refined.")
    print(f"  ⏭️ {skipped_count} files were skipped (no compatible columns or errors).")

if __name__ == "__main__":
    process_raw_directory()