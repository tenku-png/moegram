#pragma once

#include "base/basic_types.h"

namespace MoeGram::Media {

struct RoundVideoSource {
	QString path;
	// Part of the shown (rotated) frame, in 0..1 coordinates.
	QRectF crop = QRectF(0., 0., 1., 1.);
	crl::time from = 0;
	crl::time till = 0;
};

struct RoundVideo {
	QByteArray content;
	crl::time duration = 0;
};

// Square 400x400 H.264 + AAC mp4, at most 60 seconds. Empty on failure.
[[nodiscard]] RoundVideo ConvertToRoundVideo(const RoundVideoSource &source);

}
