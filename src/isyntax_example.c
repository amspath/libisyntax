#include "common.h"

#include "isyntax.h"

int main(int argc, char** argv) {

	if (argc <= 1) {
		return 0;
	}

	char* filename = argv[1];

	isyntax_t isyntax = {};
	if (isyntax_open(&isyntax, filename)) {
		printf("Successfully opened %s\n", filename);
	}

	return 0;
}
