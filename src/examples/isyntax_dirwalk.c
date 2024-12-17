#include "libisyntax.h"

#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <malloc.h>


#define CHECK_LIBISYNTAX_OK(_libisyntax_call) do { \
    isyntax_error_t result = _libisyntax_call;     \
    assert(result == LIBISYNTAX_OK);               \
} while(0)


#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/";
#endif

// This program loops through all the .isyntax files in a folder (including subfolders),
// and outputs the barcode for each file to stdout (in comma-separated format).
// This may be useful for large collections of WSIs where you can't tell which is which from the filename.

// NOTE: the barcode/label information must be preserved for this to work.
// This information might be deleted when using the Philips IMS export function (unless that setting is changed).

int read_barcode_of_isyntax_files_in_directory(const char* dir, const char* subdir_prefix) {
    // Looping through files in a folder.
    // Reference: https://stackoverflow.com/questions/1271064/how-do-i-loop-through-all-files-in-a-folder-using-c
    struct dirent *dp;
	DIR *dfd;
	if ((dfd = opendir(dir)) == NULL) {
		fprintf(stderr, "Can't open %s\n", dir);
		return 0;
	}

	char filename[260] ;
	while ((dp = readdir(dfd)) != NULL) {
		struct stat stbuf ;
		sprintf(filename, "%s" PATH_SEP "%s", dir, dp->d_name);
		if(stat(filename, &stbuf) == -1) {
			printf("Unable to stat file: %s\n" , filename);
			continue ;
		}
		if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
			if (dp->d_name[0] != '.') {
				char* subdir_path = malloc(260);
				snprintf(subdir_path, 260, "%s" PATH_SEP "%s", dir, dp->d_name);
				read_barcode_of_isyntax_files_in_directory(subdir_path, dp->d_name); // recurse into subdirectories
				free(subdir_path);
			}
			continue; // Skip directories
		} else {
			size_t filename_len = strlen(dp->d_name);
			const char* suffix = ".isyntax";
			size_t suffix_len = strlen(suffix);
			if (strncmp(dp->d_name + filename_len - suffix_len, suffix, suffix_len) == 0) {
                // Open the iSyntax file in a special mode where the header is only read partially (for speed).
				isyntax_t* isyntax;
                if (libisyntax_open(filename, LIBISYNTAX_OPEN_FLAG_READ_BARCODE_ONLY, &isyntax) == LIBISYNTAX_OK) {
                    // Print out the path of the .isyntax file + the barcode in .csv format.
                    if (subdir_prefix) {
						printf("%s" PATH_SEP "%s,%s\n", subdir_prefix, dp->d_name, libisyntax_get_barcode(isyntax));
					} else {
						printf("%s,%s\n", dp->d_name, libisyntax_get_barcode(isyntax));
					}
					libisyntax_close(isyntax);
				}
			}
		}
	}
    return 0;
}

int main(int argc, char** argv) {
	if (argc <= 1) {
		printf("Usage: %s <directory_path> - output filename and barcode (comma-separated) for each iSyntax file in the directory and its subdirectories\n",
		       argv[0], argv[0], argv[0], argv[0]);
		return 0;
	}

	libisyntax_init();

	char* dir = argv[1];
	return read_barcode_of_isyntax_files_in_directory(dir, NULL);
}
