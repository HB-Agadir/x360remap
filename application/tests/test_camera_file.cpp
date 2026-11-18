// cl /EHsc /I.. test_camera_file.cpp ../../X360Remap/camera_learn.cpp && test_camera_file
#include <assert.h>
#include <stdio.h>
#include <string>
#include "../camera_file.h"

static std::string ReadAll(const char* path) {
	std::string s;
	FILE* f = fopen(path, "rb");
	int c;
	while (f && (c = fgetc(f)) != EOF) s += (char)c;
	if (f) fclose(f);
	return s;
}

int main() {
	const char* path = "test_camera.txt";
	const char* bak = "test_camera.bak";
	const std::string text =
		"# comment 425307D3\n"
		"v2 425307D3 4AD9EB64 00920000 1 8289EAF8 00000000 00000590 00000000 -1 1 rad Title 425307D3\n"
		"v2 54510842 4B6AD04C 00A60000 1 829B94E0 00000000 00000354 FFFFFFFC 1 -1 rad Title 54510842\n";
	FILE* f = fopen(path, "wb");
	fputs(text.c_str(), f);
	fclose(f);

	assert(CameraFileState(path, 0x425307D3) == 1);   // horizontal only
	assert(CameraFileState(path, 0x54510842) == 2);   // both axes
	assert(CameraFileState(path, 0x12345678) == 0);
	assert(CameraFileState("missing.txt", 0x425307D3) == 0);

	assert(!CameraFileRemove(path, bak, 0x12345678));
	assert(ReadAll(path) == text);
	assert(CameraFileRemove(path, bak, 0x425307D3));
	assert(CameraFileState(path, 0x425307D3) == 0);
	assert(CameraFileState(path, 0x54510842) == 2);
	assert(ReadAll(path).find("# comment") == 0);     // comments stay
	assert(ReadAll(bak) == text);

	remove(path);
	remove(bak);
	printf("camera file OK\n");
	return 0;
}
