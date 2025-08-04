import os
from typing import List

from tqdm import tqdm
import bs4
import requests


DATASETS = [
    'CyclicJoinBench',
    'NextiaJD',
    'PublicBIbenchmark'
]

DATA_PATH = '../../../benchmarking/data/raw/'


def get_file_paths_of_dir(directory_path: str, recursive: bool = True) -> List[str]:
    """
    Get all file paths of a directory.
    :param directory_path: The path to the directory.
    :param recursive: If True, also get the file paths of subdirectories.
    :return: A list of file paths.
    """
    html = requests.get(directory_path).text
    soup = bs4.BeautifulSoup(html, 'html.parser')
    hrefs = [a['href'] for a in soup.find_all('a', href=True)]
    # href is valid if it is relative and does not contain http parameters
    hrefs_filtered = [href for href in hrefs if not href.startswith('/') and '?' not in href]
    hrefs_filtered_abs = [directory_path + href for href in hrefs_filtered]

    dirs = [href for href in hrefs_filtered_abs if href.endswith('/')]
    files = [href for href in hrefs_filtered_abs if not href.endswith('/')]

    if recursive:
        for sub_dir in dirs:
            files_of_dir = get_file_paths_of_dir(sub_dir, recursive=True)
            files.extend(files_of_dir)

    return files


def download_data(dataset: str):
    url = "https://event.cwi.nl/da/" + dataset + "/"
    print('Searching for files to download...')
    files = get_file_paths_of_dir(url)
    print(f'Found {len(files)} files to download. Starting download...')
    for file_path in tqdm(files, desc='Downloading files'):
        file_path_local = file_path.replace(url, DATA_PATH + dataset + '/')
        os.makedirs(os.path.dirname(file_path_local), exist_ok=True)
        with open(file_path_local, 'wb') as f:
            response = requests.get(file_path)
            f.write(response.content)


def main():
    for dataset in DATASETS:
        download_data(dataset)



if __name__ == '__main__':
    main()