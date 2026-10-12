#include "moegram/ui/plugin_header.h"

#include "moegram/ui/plugin_ui.h"
#include "ui/painter.h"
#include "styles/style_basic.h"
#include "styles/style_window.h"
#include "styles/style_info.h"

#include <QtCore/QJsonArray>
#include <QtGui/QMouseEvent>

namespace MoeGram::Ui {
namespace {

constexpr auto kMaxParts = 6;
constexpr auto kMaxText = 24;

} // namespace

ChatHeaderDecor::ChatHeaderDecor(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
	PluginValueChanges(
		u"chat_header"_q
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
	refresh();
}

void ChatHeaderDecor::refresh() {
	_parts.clear();
	_subtitle = QString();
	_background = std::nullopt;
	_opacity = 1.;
	for (const auto &[pluginId, value] : PluginValues(u"chat_header"_q)) {
		const auto spec = value.toObject();
		if (const auto subtitle = spec.value(u"subtitle"_q).toString()
				.simplified(); !subtitle.isEmpty()) {
			_subtitle = subtitle.left(64);
		}
		if (const auto color = ParseColor(spec.value(u"background"_q))) {
			_background = color;
		}
		if (spec.contains(u"opacity"_q)) {
			_opacity = std::clamp(spec.value(u"opacity"_q).toDouble(1.), 0., 1.);
		}
		const auto add = [&](const QJsonValue &list, bool button) {
			for (const auto &entry : list.toArray()) {
				const auto object = entry.toObject();
				const auto text = object.value(u"text"_q).toString().simplified();
				if (text.isEmpty() || _parts.size() >= kMaxParts) {
					continue;
				}
				_parts.push_back({
					.pluginId = pluginId,
					.text = text.left(kMaxText),
					.callback = button ? object.value(u"on_click"_q) : QJsonValue(),
					.color = ParseColor(object.value(u"color"_q)),
					.bg = ParseColor(object.value(u"background"_q)),
				});
			}
		};
		add(spec.value(u"badges"_q), false);
		add(spec.value(u"buttons"_q), true);
	}
	const auto &font = st::semiboldFont;
	const auto padding = font->height / 2;
	const auto height = st::topBarHeight;
	const auto pill = font->height + padding;
	auto x = 0;
	for (auto &part : _parts) {
		const auto w = font->width(part.text) + 2 * padding;
		part.rect = QRect(x, (height - pill) / 2, w, pill);
		x += w + padding;
	}
	_over = -1;
	setCursor(style::cur_default);
	resize(x, height);
	update();
	_changes.fire({});
}

int ChatHeaderDecor::partAt(QPoint point) const {
	for (auto i = 0; i != int(_parts.size()); ++i) {
		if (_parts[i].rect.contains(point)) {
			return i;
		}
	}
	return -1;
}

void ChatHeaderDecor::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);
	p.setFont(st::semiboldFont);
	for (auto i = 0; i != int(_parts.size()); ++i) {
		const auto &part = _parts[i];
		const auto button = !part.callback.isNull()
			&& !part.callback.isUndefined();
		auto bg = part.bg.value_or(st::windowBgActive->c);
		if (!part.bg) {
			bg.setAlphaF((button && i == _over) ? 0.24 : 0.14);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		const auto radius = part.rect.height() / 2.;
		p.drawRoundedRect(part.rect, radius, radius);
		p.setPen(part.color.value_or(st::windowActiveTextFg->c));
		p.drawText(part.rect, Qt::AlignCenter, part.text);
	}
}

void ChatHeaderDecor::mouseMoveEvent(QMouseEvent *e) {
	const auto over = partAt(e->pos());
	if (_over != over) {
		_over = over;
		const auto button = (over >= 0) && _parts[over].callback.isObject();
		setCursor(button ? style::cur_pointer : style::cur_default);
		update();
	}
}

void ChatHeaderDecor::mousePressEvent(QMouseEvent *e) {
	const auto index = partAt(e->pos());
	if (e->button() == Qt::LeftButton && index >= 0) {
		const auto &part = _parts[index];
		InvokePluginCallback(part.pluginId, part.callback, {
			{ u"text"_q, part.text },
		});
	}
}

void ChatHeaderDecor::leaveEventHook(QEvent *e) {
	if (_over >= 0) {
		_over = -1;
		setCursor(style::cur_default);
		update();
	}
}

}
