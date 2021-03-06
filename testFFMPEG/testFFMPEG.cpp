#include "../ffmpeg/ffmpeg.h"

#include <vector>
#include <iostream>

int main(int argc, char **argv)
{
	const char *filename;

	if (argc <= 2)
	{
		std::cerr << "Usage: " << argv[0] << " <output file>" << std::endl;
		return 0;
	}
	filename = argv[1];

	const size_t width = 352;
	const size_t height = 288;

	ffmpeg encoder(filename, width, height, 25);
	std::vector<uint8_t> Y(encoder.yLineSize() * encoder.height());
	std::vector<uint8_t> U(encoder.uLineSize() * encoder.height() / 2);
	std::vector<uint8_t> V(encoder.vLineSize() * encoder.height() / 2);

	/* encode 1 second of video */
	for (int i = 0; i < 25; i++)
	{
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				Y[y * encoder.yLineSize() + x] = x + y + i * 3;
			}
		}

		/* Cb and Cr */
		for (int y = 0; y < height / 2; y++)
		{
			for (int x = 0; x < width / 2; x++)
			{
				U[y * encoder.uLineSize() + x] = 128 + y + i * 2;
				V[y * encoder.vLineSize() + x] = 64 + x + i * 5;
			}
		}
		encoder.addFrame(Y.data(), U.data(), V.data());
	}

	return 0;
}
