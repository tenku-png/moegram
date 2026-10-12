#include "moegram/media/round_video_converter.h"

#include "base/random.h"
#include "ffmpeg/ffmpeg_utility.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtGui/QImage>
#include <QtGui/QTransform>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
} // extern "C"

namespace MoeGram::Media {
namespace {

constexpr auto kSide = 400;
constexpr auto kMaxDuration = 60 * crl::time(1000);
constexpr auto kFrameInterval = crl::time(1000) / 30;
constexpr auto kVideoBitRate = 1000 * 1024;
constexpr auto kAudioBitRate = 64 * 1024;
constexpr auto kAudioFrequency = 48'000;
constexpr auto kMilliseconds = AVRational{ 1, 1000 };

struct InputDeleter {
	void operator()(AVFormatContext *value) const {
		avformat_close_input(&value);
	}
};
struct OutputDeleter {
	void operator()(AVFormatContext *value) const {
		if (value->pb) {
			avio_closep(&value->pb);
		}
		avformat_free_context(value);
	}
};
struct CodecDeleter {
	void operator()(AVCodecContext *value) const {
		avcodec_free_context(&value);
	}
};
struct FrameDeleter {
	void operator()(AVFrame *value) const {
		av_frame_free(&value);
	}
};
struct PacketDeleter {
	void operator()(AVPacket *value) const {
		av_packet_free(&value);
	}
};
struct ScaleDeleter {
	void operator()(SwsContext *value) const {
		sws_freeContext(value);
	}
};
struct ResampleDeleter {
	void operator()(SwrContext *value) const {
		swr_free(&value);
	}
};
struct FifoDeleter {
	void operator()(AVAudioFifo *value) const {
		av_audio_fifo_free(value);
	}
};

using Input = std::unique_ptr<AVFormatContext, InputDeleter>;
using Output = std::unique_ptr<AVFormatContext, OutputDeleter>;
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;
using Frame = std::unique_ptr<AVFrame, FrameDeleter>;
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;
using Scale = std::unique_ptr<SwsContext, ScaleDeleter>;
using Resample = std::unique_ptr<SwrContext, ResampleDeleter>;
using Fifo = std::unique_ptr<AVAudioFifo, FifoDeleter>;

bool Fail(const char *method, int error = 0) {
	auto buffer = std::array<char, AV_ERROR_MAX_STRING_SIZE>{};
	if (error) {
		av_strerror(error, buffer.data(), buffer.size());
	}
	LOG(("Round Video Error: %1 %2").arg(method, buffer.data()));
	return false;
}

class Converter final {
public:
	explicit Converter(const RoundVideoSource &source);

	[[nodiscard]] RoundVideo run();

private:
	[[nodiscard]] bool openInput();
	[[nodiscard]] Codec openDecoder(int index);
	[[nodiscard]] bool openOutput(const QString &path);
	[[nodiscard]] bool openVideoEncoder();
	[[nodiscard]] bool openAudioEncoder();
	[[nodiscard]] bool convert();
	[[nodiscard]] bool receive(AVCodecContext *decoder, bool video);
	[[nodiscard]] bool video(AVFrame *frame);
	[[nodiscard]] bool audio(AVFrame *frame);
	[[nodiscard]] bool encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame);
	[[nodiscard]] bool drainAudio(bool final);
	[[nodiscard]] crl::time frameTime(AVFrame *frame, AVStream *stream) const;
	[[nodiscard]] QImage frameImage(AVFrame *frame);

	const RoundVideoSource _source;
	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _start = 0;
	Input _input;
	Output _output;
	int _videoIndex = -1;
	int _audioIndex = -1;
	int _rotation = 0;
	Codec _videoDecoder;
	Codec _audioDecoder;
	Codec _videoEncoder;
	Codec _audioEncoder;
	AVStream *_videoStream = nullptr;
	AVStream *_audioStream = nullptr;
	Frame _videoFrame;
	Frame _audioFrame;
	Scale _toImage;
	Scale _toEncoder;
	Resample _resample;
	Fifo _fifo;
	int64 _audioPts = 0;
	crl::time _lastVideo = -1;
	bool _videoDone = false;
	bool _audioDone = false;

};

Converter::Converter(const RoundVideoSource &source)
: _source(source)
, _from(std::max(source.from, crl::time(0)))
, _till(std::min(source.till, _from + kMaxDuration)) {
}

bool Converter::openInput() {
	auto raw = (AVFormatContext*)nullptr;
	const auto path = QFile::encodeName(_source.path);
	if (const auto error = avformat_open_input(&raw, path.constData(), nullptr, nullptr)) {
		return Fail("avformat_open_input", error);
	}
	_input = Input(raw);
	if (const auto error = avformat_find_stream_info(raw, nullptr); error < 0) {
		return Fail("avformat_find_stream_info", error);
	}
	_videoIndex = av_find_best_stream(raw, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	_audioIndex = av_find_best_stream(raw, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (_videoIndex < 0) {
		return Fail("no video stream");
	}
	_rotation = FFmpeg::ReadRotationFromMetadata(raw->streams[_videoIndex]);
	_start = (raw->start_time != AV_NOPTS_VALUE)
		? raw->start_time / 1000
		: 0;
	_videoDecoder = openDecoder(_videoIndex);
	if (!_videoDecoder) {
		return false;
	}
	if (_audioIndex >= 0) {
		_audioDecoder = openDecoder(_audioIndex);
		if (!_audioDecoder) {
			_audioIndex = -1;
		}
	}
	return true;
}

Codec Converter::openDecoder(int index) {
	const auto parameters = _input->streams[index]->codecpar;
	const auto decoder = avcodec_find_decoder(parameters->codec_id);
	if (!decoder) {
		Fail("avcodec_find_decoder");
		return nullptr;
	}
	auto result = Codec(avcodec_alloc_context3(decoder));
	if (!result) {
		Fail("avcodec_alloc_context3");
		return nullptr;
	} else if (const auto error = avcodec_parameters_to_context(
			result.get(),
			parameters); error < 0) {
		Fail("avcodec_parameters_to_context", error);
		return nullptr;
	}
	result->pkt_timebase = _input->streams[index]->time_base;
	if (const auto error = avcodec_open2(result.get(), decoder, nullptr)) {
		Fail("avcodec_open2", error);
		return nullptr;
	}
	return result;
}

bool Converter::openVideoEncoder() {
	auto encoder = avcodec_find_encoder_by_name("libopenh264");
	if (!encoder) {
		encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!encoder) {
		return Fail("no H.264 encoder");
	}
	_videoEncoder = Codec(avcodec_alloc_context3(encoder));
	if (!_videoEncoder) {
		return Fail("avcodec_alloc_context3");
	}
	const auto codec = _videoEncoder.get();
	codec->width = kSide;
	codec->height = kSide;
	codec->pix_fmt = AV_PIX_FMT_YUV420P;
	codec->time_base = kMilliseconds;
	codec->framerate = AVRational{ 30, 1 };
	codec->bit_rate = kVideoBitRate;
	codec->gop_size = 60;
	codec->max_b_frames = 0;
	if (_output->oformat->flags & AVFMT_GLOBALHEADER) {
		codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	if (const auto error = avcodec_open2(codec, encoder, nullptr)) {
		return Fail("avcodec_open2 video", error);
	}
	_videoStream = avformat_new_stream(_output.get(), nullptr);
	if (!_videoStream) {
		return Fail("avformat_new_stream");
	}
	_videoStream->time_base = codec->time_base;
	if (avcodec_parameters_from_context(_videoStream->codecpar, codec) < 0) {
		return Fail("avcodec_parameters_from_context");
	}
	_videoFrame = Frame(av_frame_alloc());
	_videoFrame->format = codec->pix_fmt;
	_videoFrame->width = kSide;
	_videoFrame->height = kSide;
	if (const auto error = av_frame_get_buffer(_videoFrame.get(), 0)) {
		return Fail("av_frame_get_buffer", error);
	}
	_toEncoder = Scale(sws_getContext(
		kSide,
		kSide,
		AV_PIX_FMT_BGRA,
		kSide,
		kSide,
		AV_PIX_FMT_YUV420P,
		SWS_BICUBIC,
		nullptr,
		nullptr,
		nullptr));
	return _toEncoder ? true : Fail("sws_getContext");
}

bool Converter::openAudioEncoder() {
	const auto encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!encoder) {
		return Fail("no AAC encoder");
	}
	_audioEncoder = Codec(avcodec_alloc_context3(encoder));
	if (!_audioEncoder) {
		return Fail("avcodec_alloc_context3");
	}
	const auto codec = _audioEncoder.get();
	codec->sample_fmt = AV_SAMPLE_FMT_FLTP;
	codec->bit_rate = kAudioBitRate;
	codec->sample_rate = kAudioFrequency;
	codec->time_base = AVRational{ 1, kAudioFrequency };
	av_channel_layout_default(&codec->ch_layout, 1);
	if (_output->oformat->flags & AVFMT_GLOBALHEADER) {
		codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	if (const auto error = avcodec_open2(codec, encoder, nullptr)) {
		return Fail("avcodec_open2 audio", error);
	}
	_audioStream = avformat_new_stream(_output.get(), nullptr);
	if (!_audioStream) {
		return Fail("avformat_new_stream");
	}
	_audioStream->time_base = codec->time_base;
	if (avcodec_parameters_from_context(_audioStream->codecpar, codec) < 0) {
		return Fail("avcodec_parameters_from_context");
	}
	auto raw = (SwrContext*)nullptr;
	const auto decoder = _audioDecoder.get();
	if (swr_alloc_set_opts2(
			&raw,
			&codec->ch_layout,
			codec->sample_fmt,
			codec->sample_rate,
			&decoder->ch_layout,
			decoder->sample_fmt,
			decoder->sample_rate,
			0,
			nullptr) < 0) {
		return Fail("swr_alloc_set_opts2");
	}
	_resample = Resample(raw);
	if (const auto error = swr_init(raw); error < 0) {
		return Fail("swr_init", error);
	}
	_fifo = Fifo(av_audio_fifo_alloc(codec->sample_fmt, 1, codec->frame_size
		? codec->frame_size
		: 1024));
	_audioFrame = Frame(av_frame_alloc());
	return (_fifo && _audioFrame) ? true : Fail("av_audio_fifo_alloc");
}

bool Converter::openOutput(const QString &path) {
	auto raw = (AVFormatContext*)nullptr;
	const auto name = QFile::encodeName(path);
	if (avformat_alloc_output_context2(&raw, nullptr, "mp4", name.constData()) < 0
		|| !raw) {
		return Fail("avformat_alloc_output_context2");
	}
	_output = Output(raw);
	if (!openVideoEncoder()) {
		return false;
	}
	if (_audioDecoder && !openAudioEncoder()) {
		_audioIndex = -1;
		_audioDecoder = nullptr;
		_audioStream = nullptr;
	}
	if (const auto error = avio_open(&raw->pb, name.constData(), AVIO_FLAG_WRITE)) {
		return Fail("avio_open", error);
	}
	auto options = (AVDictionary*)nullptr;
	av_dict_set(&options, "movflags", "+faststart", 0);
	const auto error = avformat_write_header(raw, &options);
	av_dict_free(&options);
	return (error < 0) ? Fail("avformat_write_header", error) : true;
}

crl::time Converter::frameTime(AVFrame *frame, AVStream *stream) const {
	const auto pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
		? frame->best_effort_timestamp
		: frame->pts;
	return (pts == AV_NOPTS_VALUE)
		? crl::time(-1)
		: (av_rescale_q(pts, stream->time_base, kMilliseconds) - _start);
}

QImage Converter::frameImage(AVFrame *frame) {
	auto image = QImage(frame->width, frame->height, QImage::Format_ARGB32);
	_toImage = Scale(sws_getCachedContext(
		_toImage.release(),
		frame->width,
		frame->height,
		AVPixelFormat(frame->format),
		frame->width,
		frame->height,
		AV_PIX_FMT_BGRA,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr));
	if (!_toImage) {
		return QImage();
	}
	uint8_t *data[AV_NUM_DATA_POINTERS] = { image.bits() };
	int linesize[AV_NUM_DATA_POINTERS] = { int(image.bytesPerLine()) };
	sws_scale(
		_toImage.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		data,
		linesize);
	if (_rotation) {
		image = image.transformed(QTransform().rotate(_rotation));
	}
	return image;
}

bool Converter::video(AVFrame *frame) {
	const auto time = frameTime(frame, _input->streams[_videoIndex]);
	if (time < _from) {
		return true;
	} else if (time >= _till) {
		_videoDone = true;
		return true;
	} else if (_lastVideo >= 0 && time - _lastVideo < kFrameInterval - 2) {
		return true;
	}
	auto image = frameImage(frame);
	if (image.isNull()) {
		return Fail("frame conversion");
	}
	const auto crop = _source.crop;
	auto rect = QRect(
		int(crop.x() * image.width()),
		int(crop.y() * image.height()),
		int(crop.width() * image.width()),
		int(crop.height() * image.height())).intersected(image.rect());
	const auto side = std::min(rect.width(), rect.height());
	if (side <= 0) {
		return Fail("empty crop");
	}
	rect.setSize(QSize(side, side));
	image = image.copy(rect).scaled(
		kSide,
		kSide,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32);
	if (av_frame_make_writable(_videoFrame.get()) < 0) {
		return Fail("av_frame_make_writable");
	}
	const uint8_t *source[AV_NUM_DATA_POINTERS] = { image.constBits() };
	int stride[AV_NUM_DATA_POINTERS] = { int(image.bytesPerLine()) };
	sws_scale(
		_toEncoder.get(),
		source,
		stride,
		0,
		kSide,
		_videoFrame->data,
		_videoFrame->linesize);
	_videoFrame->pts = time - _from;
	_lastVideo = time;
	return encode(_videoEncoder.get(), _videoStream, _videoFrame.get());
}

bool Converter::audio(AVFrame *frame) {
	const auto time = frameTime(frame, _input->streams[_audioIndex]);
	if (time >= _till) {
		_audioDone = true;
		return true;
	} else if (time < _from) {
		return true;
	}
	const auto codec = _audioEncoder.get();
	const auto count = int(av_rescale_rnd(
		swr_get_delay(_resample.get(), frame->sample_rate) + frame->nb_samples,
		codec->sample_rate,
		frame->sample_rate,
		AV_ROUND_UP));
	auto converted = Frame(av_frame_alloc());
	converted->format = codec->sample_fmt;
	converted->sample_rate = codec->sample_rate;
	converted->nb_samples = count;
	av_channel_layout_copy(&converted->ch_layout, &codec->ch_layout);
	if (const auto error = av_frame_get_buffer(converted.get(), 0)) {
		return Fail("av_frame_get_buffer audio", error);
	}
	const auto written = swr_convert(
		_resample.get(),
		converted->data,
		count,
		(const uint8_t**)frame->extended_data,
		frame->nb_samples);
	if (written < 0) {
		return Fail("swr_convert", written);
	}
	if (av_audio_fifo_write(_fifo.get(), (void**)converted->data, written) < written) {
		return Fail("av_audio_fifo_write");
	}
	return drainAudio(false);
}

bool Converter::drainAudio(bool final) {
	const auto codec = _audioEncoder.get();
	const auto size = codec->frame_size ? codec->frame_size : 1024;
	while (av_audio_fifo_size(_fifo.get()) >= size
		|| (final && av_audio_fifo_size(_fifo.get()) > 0)) {
		const auto count = std::min(av_audio_fifo_size(_fifo.get()), size);
		av_frame_unref(_audioFrame.get());
		_audioFrame->format = codec->sample_fmt;
		_audioFrame->sample_rate = codec->sample_rate;
		_audioFrame->nb_samples = count;
		av_channel_layout_copy(&_audioFrame->ch_layout, &codec->ch_layout);
		if (const auto error = av_frame_get_buffer(_audioFrame.get(), 0)) {
			return Fail("av_frame_get_buffer audio", error);
		}
		if (av_audio_fifo_read(_fifo.get(), (void**)_audioFrame->data, count) < count) {
			return Fail("av_audio_fifo_read");
		}
		_audioFrame->pts = _audioPts;
		_audioPts += count;
		if (!encode(codec, _audioStream, _audioFrame.get())) {
			return false;
		}
	}
	return true;
}

bool Converter::encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame) {
	if (const auto error = avcodec_send_frame(codec, frame); error < 0) {
		return Fail("avcodec_send_frame", error);
	}
	auto packet = Packet(av_packet_alloc());
	while (true) {
		const auto error = avcodec_receive_packet(codec, packet.get());
		if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
			return true;
		} else if (error < 0) {
			return Fail("avcodec_receive_packet", error);
		}
		av_packet_rescale_ts(packet.get(), codec->time_base, stream->time_base);
		packet->stream_index = stream->index;
		if (const auto written = av_interleaved_write_frame(
				_output.get(),
				packet.get()); written < 0) {
			return Fail("av_interleaved_write_frame", written);
		}
	}
}

bool Converter::receive(AVCodecContext *decoder, bool video) {
	auto frame = Frame(av_frame_alloc());
	while (true) {
		const auto error = avcodec_receive_frame(decoder, frame.get());
		if (error == AVERROR(EAGAIN) || error == AVERROR_EOF) {
			return true;
		} else if (error < 0) {
			return Fail("avcodec_receive_frame", error);
		}
		const auto ok = video ? this->video(frame.get()) : audio(frame.get());
		av_frame_unref(frame.get());
		if (!ok) {
			return false;
		}
	}
}

bool Converter::convert() {
	if (_from > 0) {
		av_seek_frame(
			_input.get(),
			-1,
			(_from + _start) * 1000,
			AVSEEK_FLAG_BACKWARD);
	}
	auto packet = Packet(av_packet_alloc());
	while (!_videoDone || (_audioStream && !_audioDone)) {
		const auto error = av_read_frame(_input.get(), packet.get());
		if (error == AVERROR_EOF) {
			break;
		} else if (error < 0) {
			return Fail("av_read_frame", error);
		}
		const auto index = packet->stream_index;
		const auto decoder = (index == _videoIndex && !_videoDone)
			? _videoDecoder.get()
			: (index == _audioIndex && _audioStream && !_audioDone)
			? _audioDecoder.get()
			: nullptr;
		if (decoder) {
			const auto sent = avcodec_send_packet(decoder, packet.get());
			if (sent < 0 && sent != AVERROR(EAGAIN) && sent != AVERROR_INVALIDDATA) {
				av_packet_unref(packet.get());
				return Fail("avcodec_send_packet", sent);
			}
			if (!receive(decoder, decoder == _videoDecoder.get())) {
				av_packet_unref(packet.get());
				return false;
			}
		}
		av_packet_unref(packet.get());
	}
	if (!_videoDone) {
		avcodec_send_packet(_videoDecoder.get(), nullptr);
		if (!receive(_videoDecoder.get(), true)) {
			return false;
		}
	}
	if (_audioStream && !_audioDone) {
		avcodec_send_packet(_audioDecoder.get(), nullptr);
		if (!receive(_audioDecoder.get(), false)) {
			return false;
		}
	}
	if (_lastVideo < 0) {
		return Fail("no frames in the chosen part");
	}
	if (!encode(_videoEncoder.get(), _videoStream, nullptr)) {
		return false;
	}
	if (_audioStream
		&& (!drainAudio(true)
			|| !encode(_audioEncoder.get(), _audioStream, nullptr))) {
		return false;
	}
	if (const auto error = av_write_trailer(_output.get())) {
		return Fail("av_write_trailer", error);
	}
	return true;
}

RoundVideo Converter::run() {
	if (_till <= _from) {
		Fail("empty range");
		return {};
	}
	const auto path = QDir::tempPath()
		+ u"/moegram-round-%1.mp4"_q.arg(base::RandomValue<uint64>());
	const auto ok = openInput() && openOutput(path) && convert();
	_output = nullptr;
	auto result = RoundVideo();
	if (ok) {
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			result.content = file.readAll();
			result.duration = std::min(
				_till - _from,
				_lastVideo - _from + kFrameInterval);
		}
	}
	QFile::remove(path);
	return result;
}

} // namespace

RoundVideo ConvertToRoundVideo(const RoundVideoSource &source) {
	return Converter(source).run();
}

}
