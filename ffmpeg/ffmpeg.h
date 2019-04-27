#ifndef _FFMPEG_h_
#define _FFMPEG_h_ "ffmpeg.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}
#include <cstdint>

class ffmpeg
{
public:
	ffmpeg(const char* filename, int width, int height, int fps);
	ffmpeg(const ffmpeg&) = delete;
	ffmpeg(ffmpeg&&) = delete;
	~ffmpeg();

	int width() const
	{
		return m_Width;
	}

	int height() const
	{
		return m_Height;
	}

	int fps() const
	{
		return m_FPS;
	}

	int yLineSize() const
	{
		return m_Frame->linesize[0];
	}

	int uLineSize() const
	{
		return m_Frame->linesize[1];
	}

	int vLineSize() const
	{
		return m_Frame->linesize[2];
	}

	bool addFrame(const uint8_t* y, const uint8_t* u, const uint8_t* v);
private:
	void addStream();
	void openVideo();
	AVFrame* allocPicture(enum AVPixelFormat pix_fmt, int width, int height);

	AVFormatContext* m_FormatContext;
	AVOutputFormat* m_Format;

	int m_Width;
	int m_Height;
	int m_FPS;

	AVCodec* m_VideoCodec;
	AVStream* m_VideoStream;
	AVCodecContext* m_VideoContext;
	AVFrame* m_Frame;
	int64_t m_Pts;
};

#endif//_FFMPEG_h_