#pragma once

#include "base/basic_types.h"
#include "ui/style/style_core_types.h"

namespace MoeGram::Ui {

struct BlurSpec {
	int radius = 12;
	float64 opacity = 0.85;
	std::optional<QColor> tint;
};

// "menu", "chat_header", "composer", "dialogs_header".
[[nodiscard]] std::optional<BlurSpec> LookupBlur(const QString &target);

// Downscales, blurs and tints `image`, the result is small and should be
// drawn stretched over the original rect.
[[nodiscard]] QImage PrepareBackdrop(QImage image, const BlurSpec &spec);

// What is under a popup menu, from our own windows only.
[[nodiscard]] QImage MenuBackdrop(not_null<QWidget*> menu, QRect global);

enum class BlurEdge {
	Top,
	Bottom,
};

// Lets `target` paint a frosted copy of the nearest strip of `source`
// (the list it is attached to) instead of its opaque background.
void AttachEdgeBlur(
	not_null<QWidget*> target,
	const QString &key,
	Fn<QWidget*()> source,
	BlurEdge edge);

// Returns false when there is no blur, so the caller paints as usual.
bool PaintEdgeBlur(
	not_null<QWidget*> target,
	const QString &key,
	QPainter &p,
	QRect rect,
	const style::color &bg);

// Smears `viewport` while `scrollTop` changes fast ("scroll_motion_blur").
void AttachScrollMotionBlur(
	not_null<QWidget*> viewport,
	Fn<QWidget*()> content,
	rpl::producer<int> scrollTop);

}
