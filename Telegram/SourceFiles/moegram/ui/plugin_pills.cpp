#include "moegram/ui/plugin_pills.h"

#include "ui/painter.h"
#include "styles/style_basic.h"
#include "styles/style_widgets.h"

#include <QtGui/QMouseEvent>

namespace MoeGram::Ui {
namespace {

struct Store {
	std::vector<std::pair<QString, Plugins::PillState>> list;
	rpl::event_stream<> changes;
};

Store &Pills() {
	static auto result = Store();
	return result;
}

} // namespace

void SetPluginPill(QString id, std::optional<Plugins::PillState> state) {
	auto &list = Pills().list;
	const auto i = ranges::find(list, id, &std::pair<QString, Plugins::PillState>::first);
	if (!state) {
		if (i == end(list)) {
			return;
		}
		list.erase(i);
	} else {
		auto value = *state;
		if (i != end(list)) {
			i->second = value;
		} else {
			list.emplace_back(std::move(id), std::move(value));
		}
	}
	Pills().changes.fire({});
}

PluginPills::PluginPills(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
	Pills().changes.events() | rpl::on_next([=] {
		refresh();
	}, lifetime());
	refresh();
}

rpl::producer<int> PluginPills::widthValueWanted() const {
	return _wanted.value();
}

void PluginPills::setPillHeight(int height) {
	if (_height != height) {
		_height = height;
		refresh();
	}
}

void PluginPills::refresh() {
	const auto &font = st::normalFont;
	const auto padding = font->height / 2 + st::lineWidth * 2;
	const auto skip = st::lineWidth * 4;
	_pills.clear();
	auto x = 0;
	for (const auto &[id, state] : Pills().list) {
		if (state.text.isEmpty()) {
			continue;
		}
		const auto w = font->width(state.text) + 2 * padding;
		_pills.push_back({
			.id = id,
			.text = state.text,
			.clickable = state.clickable,
			.rect = QRect(x, 0, w, _height),
		});
		x += w + skip;
	}
	const auto wanted = _pills.empty() ? 0 : x;
	_over = -1;
	setCursor(style::cur_default);
	resize(wanted, _height);
	_wanted = wanted;
	update();
}

int PluginPills::pillAt(QPoint point) const {
	for (auto i = 0; i != int(_pills.size()); ++i) {
		if (_pills[i].rect.contains(point)) {
			return i;
		}
	}
	return -1;
}

void PluginPills::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	PainterHighQualityEnabler hq(p);
	const auto &font = st::normalFont;
	for (auto i = 0; i != int(_pills.size()); ++i) {
		const auto &pill = _pills[i];
		const auto rect = pill.rect.marginsRemoved(
			QMargins(0, st::lineWidth * 6, 0, st::lineWidth * 6));
		auto bg = st::windowBgActive->c;
		bg.setAlphaF((i == _over && pill.clickable) ? 0.22 : 0.14);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		const auto radius = rect.height() / 2.;
		p.drawRoundedRect(rect, radius, radius);
		p.setPen(st::windowActiveTextFg);
		p.setFont(font);
		p.drawText(rect, Qt::AlignCenter, pill.text);
	}
}

void PluginPills::mouseMoveEvent(QMouseEvent *e) {
	const auto over = pillAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over >= 0 && _pills[over].clickable)
			? style::cur_pointer
			: style::cur_default);
		update();
	}
}

void PluginPills::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto index = pillAt(e->pos());
	if (index >= 0 && _pills[index].clickable) {
		(void)Plugins::InvokePluginPill(_pills[index].id);
	}
}

void PluginPills::leaveEventHook(QEvent *e) {
	if (_over >= 0) {
		_over = -1;
		setCursor(style::cur_default);
		update();
	}
}

}
