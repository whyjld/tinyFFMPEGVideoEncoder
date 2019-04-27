#include "ffmpeg.h"

extern "C"
{
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
}

#include <stdarg.h>
#include <memory>
#include <vector>

#include <iostream>
#include <exception>

#pragma comment(lib, "avcodec")
#pragma comment(lib, "avformat")
#pragma comment(lib, "avutil")

#define THROW_ERROR(fmt, ...) \
{ \
	auto unreadablevariant_ekfjklsjflkefsddfsdfs = string_format(fmt, ##__VA_ARGS__); \
	std::cerr << unreadablevariant_ekfjklsjflkefsddfsdfs.c_str() << std::endl; \
	throw std::runtime_error(unreadablevariant_ekfjklsjflkefsddfsdfs); \
}

inline std::string avErr2Str(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE];
	return av_make_error_string(buf, AV_ERROR_MAX_STRING_SIZE, err);
}

inline std::string avTS2Str(int64_t ts)
{
	char buf[AV_TS_MAX_STRING_SIZE];
	return av_ts_make_string(buf, ts);
}
inline std::string avTS2TimeStr(int64_t ts, AVRational *tb)
{
	char buf[AV_TS_MAX_STRING_SIZE];
	return av_ts_make_time_string(buf, ts, tb);
}


std::string string_format(std::string fmt, ...)
{
	va_list ap;

	std::vector<char> buf(fmt.size() * 2);

	va_start(ap, fmt);
	size_t n = vsnprintf(&buf[0], buf.size(), fmt.c_str(), ap);
	va_end(ap);
	
	if (n >= buf.size())
	{
		buf.resize(buf.size() * 2 + 1);
		va_start(ap, fmt);
		size_t n = vsnprintf(&buf[0], buf.size(), fmt.c_str(), ap);
		va_end(ap);
	}
	return std::string(buf.data());
}

ffmpeg::ffmpeg(const char* filename, int width, int height, int fps)
	: m_FormatContext(nullptr)
	, m_Format(nullptr)
	, m_Width(width)
	, m_Height(height)
	, m_FPS(fps)
	, m_VideoCodec(nullptr)
	, m_VideoStream(nullptr)
	, m_VideoContext(nullptr)
	, m_Frame(nullptr)
	, m_Pts(0)
{
	/* allocate the output media context */
	avformat_alloc_output_context2(&m_FormatContext, nullptr, nullptr, filename);
	if (nullptr == m_FormatContext)
	{
		std::cerr << "Could not deduce output format from file extension: using MPEG." << std::endl;
		std::cerr << "Try mp4." << std::endl;
		avformat_alloc_output_context2(&m_FormatContext, nullptr, "mp4", filename);
		if (nullptr == m_FormatContext)
		{
			THROW_ERROR("Could not alloc output context.");
		}
	}

	m_Format = m_FormatContext->oformat;
	if (m_Format->video_codec != AV_CODEC_ID_NONE)
	{
		addStream();
	}

	openVideo();

	av_dump_format(m_FormatContext, 0, filename, 1);

	char msg[AV_ERROR_MAX_STRING_SIZE];

	int ret = avio_open(&m_FormatContext->pb, filename, AVIO_FLAG_WRITE);
	if (ret != 0)
	{
		THROW_ERROR("Could not open '%s': %s\n", filename, av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret));
	}
	/* Write the stream header, if any. */
	ret = avformat_write_header(m_FormatContext, nullptr);
	if (ret != 0)
	{
		THROW_ERROR("Error occurred when opening output file: %s\n", av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, ret));
	}
}


ffmpeg::~ffmpeg()
{
	/* Write the trailer, if any. The trailer must be written before you
	 * close the CodecContexts open when you wrote the header; otherwise
	 * av_write_trailer() may try to use memory that was freed on
	 * av_codec_close(). */
	av_write_trailer(m_FormatContext);

	/* Close each codec. */
	avcodec_free_context(&m_VideoContext);
	av_frame_free(&m_Frame);

	if (!(m_Format->flags & AVFMT_NOFILE))
	{
		/* Close the output file. */
		avio_closep(&m_FormatContext->pb);
	}

	/* free the stream */
	avformat_free_context(m_FormatContext);
}

/* Add an output stream. */
void ffmpeg::addStream()
{
	/* find the encoder */
	m_VideoCodec = avcodec_find_encoder(m_Format->video_codec);
	if (nullptr == m_VideoCodec)
	{
		THROW_ERROR("Could not find encoder for '%s'", avcodec_get_name(m_Format->video_codec));
	}

	m_VideoStream = avformat_new_stream(m_FormatContext, nullptr);
	if (nullptr == m_VideoStream)
	{
		THROW_ERROR("Could not allocate stream");
	}
	m_VideoStream->id = m_FormatContext->nb_streams - 1;
	m_VideoContext = avcodec_alloc_context3(m_VideoCodec);
	if (nullptr == m_VideoContext)
	{
		THROW_ERROR("Could not alloc an encoding context");
	}

	switch (m_VideoCodec->type)
	{
	case AVMEDIA_TYPE_VIDEO:
		m_VideoContext->codec_id = m_Format->video_codec;

		m_VideoContext->bit_rate = 400000;
		/* Resolution must be a multiple of two. */
		m_VideoContext->width = m_Width;
		m_VideoContext->height = m_Height;
		/* timebase: This is the fundamental unit of time (in seconds) in terms
		 * of which frame timestamps are represented. For fixed-fps content,
		 * timebase should be 1/framerate and timestamp increments should be
		 * identical to 1. */
		m_VideoStream->time_base = { 1, m_FPS };
		m_VideoContext->time_base = m_VideoStream->time_base;

		m_VideoContext->framerate = { m_FPS, 1 };

		m_VideoContext->gop_size = 12; /* emit one intra frame every twelve frames at most */
		m_VideoContext->pix_fmt = AV_PIX_FMT_YUV420P;
		switch (m_VideoContext->codec_id)
		{
		case AV_CODEC_ID_MPEG2VIDEO:
			/* just for testing, we also add B-frames */
			m_VideoContext->max_b_frames = 2;
			break;
		case AV_CODEC_ID_MPEG1VIDEO:
			/* Needed to avoid using macroblocks in which some coeffs overflow.
			 * This does not happen with normal video, it just happens here as
			 * the motion of the chroma plane does not match the luma plane. */
			m_VideoContext->mb_decision = 2;
			break;
		case AV_CODEC_ID_H264:
			av_opt_set(m_VideoContext->priv_data, "preset", "slow", 0);
			av_opt_set(m_VideoContext->priv_data, "tune", "zerolatency", 0);
			break;
		case AV_CODEC_ID_H265:
			av_opt_set(m_VideoContext->priv_data, "preset", "ultrafast", 0);
			av_opt_set(m_VideoContext->priv_data, "tune", "zero-latency", 0);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	/* Some formats want stream headers to be separate. */
	if (m_FormatContext->oformat->flags & AVFMT_GLOBALHEADER)
	{
		m_VideoContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
}

void ffmpeg::openVideo()
{
	/* open the codec */
	int ret = avcodec_open2(m_VideoContext, m_VideoCodec, nullptr);
	if (ret != 0)
	{
		THROW_ERROR("Could not open video codec: %s\n", avErr2Str(ret));
	}

	/* allocate and init a re-usable frame */
	m_Frame = allocPicture(m_VideoContext->pix_fmt, m_VideoContext->width, m_VideoContext->height);
	if (nullptr == m_Frame)
	{
		THROW_ERROR("Could not allocate video frame.");
	}

	/* copy the stream parameters to the muxer */
	ret = avcodec_parameters_from_context(m_VideoStream->codecpar, m_VideoContext);
	if (ret != 0)
	{
		THROW_ERROR("Could not copy the stream parameters");
	}
}

AVFrame* ffmpeg::allocPicture(enum AVPixelFormat pix_fmt, int width, int height)
{
	AVFrame *picture = av_frame_alloc();
	if (nullptr != picture)
	{
		picture->format = pix_fmt;
		picture->width = width;
		picture->height = height;

		/* allocate the buffers for the frame data */
		int ret = av_frame_get_buffer(picture, 32);
		if (ret != 0)
		{
			THROW_ERROR("Could not allocate frame data.");
		}
	}

	return picture;
}

bool ffmpeg::addFrame(const uint8_t* y, const uint8_t* u, const uint8_t* v)
{

	int ret = av_frame_make_writable(m_Frame);
	if (ret != 0)
	{
		std::cerr << "Error while make frame writable." << avErr2Str(ret).c_str() << std::endl;
		return false;
	}

	memcpy(m_Frame->data[0], y, m_Frame->linesize[0] * m_Frame->height);
	memcpy(m_Frame->data[1], u, m_Frame->linesize[1] * m_Frame->height / 2);
	memcpy(m_Frame->data[2], v, m_Frame->linesize[2] * m_Frame->height / 2);

	m_Frame->pts = m_Pts;
	
	AVPacket pkt = { 0 };
	av_init_packet(&pkt);

	/* send the frame to the encoder */
	if (m_Frame)
	{
		std::cout << "Send frame " << m_Frame->pts << std::endl;
	}

	ret = avcodec_send_frame(m_VideoContext, m_Frame);
	if (ret < 0)
	{
		std::cerr << "Error sending a frame for encoding." << avErr2Str(ret).c_str() << std::endl;
		return false;
	}

	while (ret >= 0)
	{
		ret = avcodec_receive_packet(m_VideoContext, &pkt);
		if (ret == AVERROR(EAGAIN))
		{
			return true;
		}
		else if (ret == AVERROR_EOF)
		{
			return true;
		}
		else if (ret < 0)
		{
			std::cerr << "Error encoding video frame: " << avErr2Str(ret).c_str() << std::endl;
			return false;
		}

		/* rescale output packet timestamp values from codec to stream timebase */
		av_packet_rescale_ts(&pkt, m_VideoContext->time_base, m_VideoStream->time_base);
		pkt.stream_index = m_VideoStream->index;

		AVRational *time_base = &m_FormatContext->streams[pkt.stream_index]->time_base;

		std::cout << "pts:" << avTS2Str(pkt.pts).c_str() 
			<< " pts_time:" << avTS2TimeStr(pkt.pts, time_base).c_str()
			<< " dts:" << avTS2Str(pkt.dts).c_str()
			<< " dts_time:" << avTS2TimeStr(pkt.dts, time_base).c_str()
			<< " duration:" << avTS2Str(pkt.duration).c_str() 
			<< " duration_time:" << avTS2TimeStr(pkt.duration, time_base).c_str()
			<< " stream_index:" << pkt.stream_index << std::endl;

		++m_Pts;
		/* Write the compressed frame to the media file. */
		return av_interleaved_write_frame(m_FormatContext, &pkt) == 0;
	}
}