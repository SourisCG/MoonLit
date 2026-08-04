#include "FfmpegTimelineExporter.hpp"

#include "ExportMath.hpp"
#include "FFmpegPipeline.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace MoonLit {
namespace {

using namespace ffmpeg;

constexpr qint64 kTimelineStartToleranceMs = 500;
constexpr qint64 kTimelineEndToleranceMs = 10000;

bool sourceExists(const TimelineSegmentSource &segment, QString *error)
{
	const QFileInfo info(segment.mediaPath);
	if (!info.exists() || !info.isFile()) {
		*error = QStringLiteral("Timeline segment media does not exist: %1").arg(segment.mediaPath);
		return false;
	}
	return true;
}

} // namespace

TimelineExportResult FfmpegTimelineExporter::exportTimeline(const TimelineExportRequest &request,
							    CancelCallback shouldCancel) const
{
	TimelineExportResult result;
	result.outputPath = request.destinationPath;

	if (request.destinationPath.isEmpty() || request.segments.isEmpty()) {
		result.error = QStringLiteral("A timeline export needs a destination and at least one segment");
		return result;
	}
	for (const TimelineSegmentSource &segment : request.segments) {
		if (!exportmath::isRangeValid(segment.sourceStartMs, segment.sourceEndMs)) {
			result.error = QStringLiteral("A timeline segment has an invalid range");
			return result;
		}
		if (samePath(segment.mediaPath, request.destinationPath)) {
			result.error = QStringLiteral("Export destination must differ from all segment sources");
			return result;
		}
		if (!sourceExists(segment, &result.error)) {
			return result;
		}
	}
	if (shouldCancel && shouldCancel()) {
		result.cancelled = true;
		result.error = QStringLiteral("Export cancelled");
		return result;
	}

	const QFileInfo destinationInfo(request.destinationPath);
	if (!QDir().mkpath(destinationInfo.absolutePath())) {
		result.error = QStringLiteral("Unable to create export directory: %1")
					.arg(destinationInfo.absolutePath());
		return result;
	}
	if (QFile::exists(request.destinationPath) && !QFile::remove(request.destinationPath)) {
		result.error = QStringLiteral("Unable to replace previous export: %1").arg(request.destinationPath);
		return result;
	}

	const QString partPath = request.destinationPath + QStringLiteral(".part");
	if (QFile::exists(partPath) && !QFile::remove(partPath)) {
		result.error = QStringLiteral("Unable to remove previous partial export: %1").arg(partPath);
		return result;
	}

	/* Probe the first segment for the output geometry and audio presence. */
	FormatContextPtr probe;
	{
		AVFormatContext *rawProbe = nullptr;
		const QByteArray encodedFirst = QFile::encodeName(request.segments.first().mediaPath);
		if (avformat_open_input(&rawProbe, encodedFirst.constData(), nullptr, nullptr) < 0 || !rawProbe) {
			result.error = QStringLiteral("Unable to open the first timeline segment");
			return result;
		}
		probe.reset(rawProbe);
		if (avformat_find_stream_info(probe.get(), nullptr) < 0) {
			result.error = QStringLiteral("Unable to read the first timeline segment streams");
			return result;
		}
	}

	AVStream *probeVideo = nullptr;
	AVStream *probeAudio = nullptr;
	for (unsigned int index = 0; index < probe->nb_streams; ++index) {
		AVStream *stream = probe->streams[index];
		if (!probeVideo && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			probeVideo = stream;
		} else if (!probeAudio && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			probeAudio = stream;
		}
	}
	if (!probeVideo) {
		result.error = QStringLiteral("The first timeline segment has no video stream");
		return result;
	}
	const bool hasAudio = probeAudio != nullptr;

	OutputContext output;
	auto fail = [&](const QString &message, bool cancelled = false) {
		output.close();
		QFile::remove(partPath);
		result.cancelled = cancelled;
		result.error = message;
		ffmpeg::appendExportLog(request.destinationPath,
					QStringLiteral("timeline %1 durationMs=%2")
						.arg(cancelled ? QStringLiteral("cancelled")
							       : QStringLiteral("failed"),
						     QString::number(result.durationMs)) +
						QStringLiteral(" error=") + message);
		return result;
	};

	const QByteArray encodedDestination = QFile::encodeName(request.destinationPath);
	int outputContextResult =
		avformat_alloc_output_context2(&output.context, nullptr, nullptr, encodedDestination.constData());
	if (outputContextResult < 0 || !output.context) {
		return fail(QStringLiteral("Unable to select export container: %1").arg(ffmpegError(outputContextResult)));
	}

	const AVCodec *videoEncoderCodec = avcodec_find_encoder_by_name("libx264");
	if (!videoEncoderCodec) {
		videoEncoderCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!videoEncoderCodec) {
		return fail(QStringLiteral("No H.264 video encoder is available for timeline export"));
	}

	AVStream *outputVideo = avformat_new_stream(output.context, nullptr);
	if (!outputVideo) {
		return fail(QStringLiteral("Unable to allocate the timeline video stream"));
	}
	CodecContextPtr videoEncoder(avcodec_alloc_context3(videoEncoderCodec));
	videoEncoder->width = probeVideo->codecpar->width;
	videoEncoder->height = probeVideo->codecpar->height;
	videoEncoder->pix_fmt = AV_PIX_FMT_YUV420P;
	videoEncoder->time_base = AVRational{1, 90000};
	const AVRational frameRate = av_guess_frame_rate(probe.get(), probeVideo, nullptr);
	if (frameRate.num > 0 && frameRate.den > 0) {
		videoEncoder->framerate = frameRate;
	}
	videoEncoder->bit_rate = 10000000;
	videoEncoder->gop_size = 60;
	videoEncoder->max_b_frames = 2;
	videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	if (avcodec_open2(videoEncoder.get(), videoEncoderCodec, nullptr) < 0) {
		return fail(QStringLiteral("Unable to open the H.264 encoder for timeline export"));
	}
	avcodec_parameters_from_context(outputVideo->codecpar, videoEncoder.get());
	outputVideo->codecpar->codec_tag = 0;
	outputVideo->time_base = videoEncoder->time_base;

	AVStream *outputAudio = nullptr;
	AudioEncodeStream audioPipeline;
	if (hasAudio) {
		const AVCodec *aacEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!aacEncoder) {
			return fail(QStringLiteral("Timeline export requires an AAC encoder, which is unavailable"));
		}
		outputAudio = avformat_new_stream(output.context, nullptr);
		if (!outputAudio) {
			return fail(QStringLiteral("Unable to allocate the timeline audio stream"));
		}
		QString audioError;
		if (!audioPipeline.initialize(probeAudio, outputAudio, aacEncoder, &audioError)) {
			return fail(audioError);
		}
	}

	if (!(output.context->oformat->flags & AVFMT_NOFILE)) {
		const QByteArray encodedPart = QFile::encodeName(partPath);
		const int ioResult = avio_open(&output.context->pb, encodedPart.constData(), AVIO_FLAG_WRITE);
		if (ioResult < 0) {
			return fail(QStringLiteral("Unable to create partial export: %1").arg(ffmpegError(ioResult)));
		}
	}
	const int headerResult = avformat_write_header(output.context, nullptr);
	if (headerResult < 0) {
		return fail(QStringLiteral("Unable to write export header: %1").arg(ffmpegError(headerResult)));
	}
	output.headerWritten = true;

	/* Total expected duration for progress and verification. */
	qint64 totalMs = 0;
	for (const TimelineSegmentSource &segment : request.segments) {
		if (segment.sourceEndMs >= 0) {
			totalMs += segment.sourceEndMs - segment.sourceStartMs;
			continue;
		}
		AVFormatContext *rawDuration = nullptr;
		const QByteArray encodedSource = QFile::encodeName(segment.mediaPath);
		if (avformat_open_input(&rawDuration, encodedSource.constData(), nullptr, nullptr) < 0 || !rawDuration) {
			return fail(QStringLiteral("Unable to open a timeline segment"));
		}
		FormatContextPtr durationInput(rawDuration);
		totalMs += std::max<qint64>(0, mediaDurationMs(*durationInput) - segment.sourceStartMs);
	}

	qint64 timelineStartMs = 0;
	bool cancelled = false;
	for (int segmentIndex = 0; segmentIndex < request.segments.size(); ++segmentIndex) {
		const TimelineSegmentSource &segment = request.segments[segmentIndex];
		qint64 segmentLengthMs = segment.sourceEndMs >= 0 ? segment.sourceEndMs - segment.sourceStartMs : -1;

		FormatContextPtr input;
		AVStream *videoStream = nullptr;
		AVStream *audioStream = nullptr;
		{
			AVFormatContext *rawInput = nullptr;
			const QByteArray encodedSource = QFile::encodeName(segment.mediaPath);
			const int openResult = avformat_open_input(&rawInput, encodedSource.constData(), nullptr, nullptr);
			if (openResult < 0) {
				return fail(QStringLiteral("Unable to open segment source: %1")
						.arg(ffmpegError(openResult)));
			}
			input.reset(rawInput);
			if (avformat_find_stream_info(input.get(), nullptr) < 0) {
				return fail(QStringLiteral("Unable to read segment streams"));
			}
			for (unsigned int index = 0; index < input->nb_streams; ++index) {
				AVStream *stream = input->streams[index];
				if (!videoStream && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
					videoStream = stream;
				} else if (!audioStream && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
					audioStream = stream;
				}
			}
		}
		if (!videoStream) {
			return fail(QStringLiteral("A timeline segment has no video stream"));
		}
		if (hasAudio && !audioStream) {
			return fail(QStringLiteral("All timeline segments must contain audio when the first one does"));
		}
		if (segmentLengthMs < 0) {
			segmentLengthMs = std::max<qint64>(0, mediaDurationMs(*input) - segment.sourceStartMs);
		}

		/* Decode everything; the first frame at or after the segment start is
		 * where the output begins (keyframe-aligned via backward seek). */
		const int64_t startUs = av_rescale_q(segment.sourceStartMs, AVRational{1, 1000}, AV_TIME_BASE_Q);
		const int64_t endUs = segment.sourceEndMs >= 0
					      ? av_rescale_q(segment.sourceEndMs, AVRational{1, 1000}, AV_TIME_BASE_Q)
					      : AV_NOPTS_VALUE;
		if (startUs > 0) {
			avformat_seek_file(input.get(), -1, std::numeric_limits<int64_t>::min(), startUs,
					   std::numeric_limits<int64_t>::max(), AVSEEK_FLAG_BACKWARD);
		}

		const AVCodec *videoDecoderCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
		if (!videoDecoderCodec) {
			return fail(QStringLiteral("No video decoder available for a segment"));
		}
		CodecContextPtr videoDecoder(avcodec_alloc_context3(videoDecoderCodec));
		if (!videoDecoder || avcodec_parameters_to_context(videoDecoder.get(), videoStream->codecpar) < 0 ||
		    avcodec_open2(videoDecoder.get(), videoDecoderCodec, nullptr) < 0) {
			return fail(QStringLiteral("Unable to open the segment video decoder"));
		}

		/* Per-segment audio decoder/resampler; the shared AAC encoder receives
		 * timeline-absolute frame timestamps so delayed packets stay
		 * monotonic across segment boundaries. */
		if (hasAudio && audioStream) {
			QString audioError;
			if (!audioPipeline.initializeSegment(audioStream, &audioError)) {
				return fail(audioError);
			}
			audioPipeline.setFramePtsOffset(
				av_rescale_q(timelineStartMs - segment.sourceStartMs, AVRational{1, 1000},
					     AV_TIME_BASE_Q));
			audioPipeline.setOutputOffset(0);
		}

		FramePtr decoded(av_frame_alloc());
		PacketPtr packet(av_packet_alloc());
		if (!decoded || !packet) {
			return fail(QStringLiteral("Unable to allocate segment decode buffers"));
		}

		bool videoEnded = false;
		bool audioEnded = false;
		int readResult = 0;
		while (!videoEnded && (readResult = av_read_frame(input.get(), packet.get())) >= 0) {
			if (shouldCancel && shouldCancel()) {
				cancelled = true;
				av_packet_unref(packet.get());
				break;
			}

			if (packet->stream_index == videoStream->index) {
				const int sendResult = avcodec_send_packet(videoDecoder.get(), packet.get());
				av_packet_unref(packet.get());
				if (sendResult < 0 && sendResult != AVERROR(EAGAIN)) {
					return fail(QStringLiteral("Unable to decode segment video: %1")
							.arg(ffmpegError(sendResult)));
				}
				while (avcodec_receive_frame(videoDecoder.get(), decoded.get()) == 0) {
					const int64_t frameUs = decoded->pts != AV_NOPTS_VALUE
									? av_rescale_q(decoded->pts, videoStream->time_base,
										       AV_TIME_BASE_Q)
									: AV_NOPTS_VALUE;
					if (frameUs != AV_NOPTS_VALUE && frameUs >= startUs) {
						if (endUs != AV_NOPTS_VALUE && frameUs >= endUs) {
							videoEnded = true;
							av_frame_unref(decoded.get());
							break;
						}
						decoded->pts = av_rescale_q(
							timelineStartMs + (frameUs - startUs) / 1000, AVRational{1, 1000},
							videoEncoder->time_base);
						const int encodeResult = avcodec_send_frame(videoEncoder.get(), decoded.get());
						if (encodeResult < 0 && encodeResult != AVERROR(EAGAIN)) {
							av_frame_unref(decoded.get());
							return fail(QStringLiteral("Unable to encode segment video: %1")
									.arg(ffmpegError(encodeResult)));
						}
						int receiveResult;
					while ((receiveResult =
							avcodec_receive_packet(videoEncoder.get(), packet.get())) == 0) {
							av_packet_rescale_ts(packet.get(), videoEncoder->time_base,
									     outputVideo->time_base);
							packet->stream_index = outputVideo->index;
							const int writeResult =
								av_interleaved_write_frame(output.context, packet.get());
							av_packet_unref(packet.get());
							if (writeResult < 0) {
								return fail(QStringLiteral("Unable to write segment video: %1")
										.arg(ffmpegError(writeResult)));
							}
						}
					}
					av_frame_unref(decoded.get());
					if (videoEnded) {
						break;
					}
				}
			} else if (hasAudio && audioStream && packet->stream_index == audioStream->index) {
				int64_t streamEndUs = endUs;
				const int audioResult = audioPipeline.pushPacket(packet.get(), output.context, audioStream,
										   streamEndUs,
										   segment.muted ? 0.0
												 : exportmath::linearGainDb(segment.gainDb),
										   audioEnded);
				av_packet_unref(packet.get());
				if (audioResult < 0) {
					return fail(QStringLiteral("Unable to encode segment audio: %1")
							.arg(ffmpegError(audioResult)));
				}
			} else {
				av_packet_unref(packet.get());
			}

			if (request.progress) {
				const double fraction =
					std::clamp((timelineStartMs + segmentLengthMs) / static_cast<double>(totalMs),
						   0.0, 1.0);
				request.progress(fraction);
			}
		}
		if (cancelled) {
			return fail(QStringLiteral("Export cancelled"), true);
		}
		if (readResult < 0 && readResult != AVERROR_EOF) {
			return fail(QStringLiteral("Unable to read a segment packet: %1").arg(ffmpegError(readResult)));
		}

		/* Flush this segment's audio pipeline (drop frames past the end);
		 * the shared AAC encoder is only flushed after the last segment. */
		if (hasAudio) {
			const int drainResult = audioPipeline.drain(
				output.context, segment.muted ? 0.0 : exportmath::linearGainDb(segment.gainDb), endUs,
				segmentIndex == request.segments.size() - 1);
			if (drainResult < 0) {
				return fail(QStringLiteral("Unable to finish segment audio: %1")
						.arg(ffmpegError(drainResult)));
			}
		}

		timelineStartMs += segmentLengthMs > 0 ? segmentLengthMs : 0;
	}

	/* Flush the video encoder. */
	PacketPtr flushPacket(av_packet_alloc());
	if (avcodec_send_frame(videoEncoder.get(), nullptr) >= 0) {
		while (avcodec_receive_packet(videoEncoder.get(), flushPacket.get()) == 0) {
			av_packet_rescale_ts(flushPacket.get(), videoEncoder->time_base, outputVideo->time_base);
			flushPacket->stream_index = outputVideo->index;
			const int writeResult = av_interleaved_write_frame(output.context, flushPacket.get());
			av_packet_unref(flushPacket.get());
			if (writeResult < 0) {
				return fail(QStringLiteral("Unable to flush segment video: %1")
						.arg(ffmpegError(writeResult)));
			}
		}
	}

	output.close();
	if (!QFileInfo::exists(partPath) || QFileInfo(partPath).size() <= 0) {
		return fail(QStringLiteral("Timeline export produced an empty file"));
	}

	QString verificationError;
	if (!verifyOutput(partPath, result.durationMs, verificationError)) {
		return fail(verificationError);
	}
	if (!exportmath::durationMatches(result.durationMs, totalMs, kTimelineStartToleranceMs,
					 kTimelineEndToleranceMs)) {
		return fail(QStringLiteral("Timeline export duration (%1 ms) does not match the expected total (%2 ms)")
				    .arg(result.durationMs)
				    .arg(totalMs));
	}

	if (!QFile::rename(partPath, request.destinationPath)) {
		return fail(QStringLiteral("Unable to atomically rename timeline export into place"));
	}

	result.succeeded = true;
	ffmpeg::appendExportLog(request.destinationPath,
				QStringLiteral("timeline ok durationMs=%1").arg(result.durationMs));
	return result;
}

} // namespace MoonLit
