#include "moegram/ui/plugin_anchors.h"

#include "base/timer.h"
#include "moegram/ui/plugin_ui.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_basic.h"

#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtGui/QImageReader>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QGraphicsOpacityEffect>

namespace MoeGram::Ui {
namespace {

constexpr auto kMaxImageSide = 1024;
constexpr auto kMaxImageBytes = 4 * 1024 * 1024;

struct Overlay {
	QString pluginId;
	int id = 0;
	QRect rect;
	QString text;
	int fontSize = 0;
	int align = Qt::AlignCenter;
	int radius = 0;
	std::optional<QColor> color;
	std::optional<QColor> background;
	QImage image;
	QJsonValue callback;
};

QImage LoadImage(const QString &path) {
	static auto cache = std::map<QString, QImage>();
	if (const auto i = cache.find(path); i != end(cache)) {
		return i->second;
	}
	auto result = QImage();
	const auto info = QFileInfo(path);
	if (info.isFile() && info.size() <= kMaxImageBytes) {
		auto reader = QImageReader(path);
		const auto size = reader.size();
		if (size.isValid()
			&& size.width() <= 4 * kMaxImageSide
			&& size.height() <= 4 * kMaxImageSide) {
			result = reader.read();
			if (result.width() > kMaxImageSide
				|| result.height() > kMaxImageSide) {
				result = result.scaled(
					kMaxImageSide,
					kMaxImageSide,
					Qt::KeepAspectRatio,
					Qt::SmoothTransformation);
			}
		}
	}
	if (cache.size() > 64) {
		cache.clear();
	}
	return cache[path] = result;
}

int Align(const QString &value) {
	auto result = 0;
	if (value.contains(u"left"_q)) result |= Qt::AlignLeft;
	else if (value.contains(u"right"_q)) result |= Qt::AlignRight;
	else result |= Qt::AlignHCenter;
	if (value.contains(u"top"_q)) result |= Qt::AlignTop;
	else if (value.contains(u"bottom"_q)) result |= Qt::AlignBottom;
	else result |= Qt::AlignVCenter;
	return result;
}

class AnchorLayer;

struct Registry {
	std::map<QString, std::vector<QPointer<QWidget>>> widgets;
	std::map<QString, std::map<QString, QJsonObject>> states;
};

Registry &Anchors() {
	static auto result = Registry();
	return result;
}

// Lives as a transparent child over the anchor widget: paints overlays,
// watches events, owns the opacity effect.
class AnchorLayer final : public ::Ui::RpWidget {
public:
	AnchorLayer(not_null<QWidget*> anchor, QString name);

	void apply();

protected:
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void send(const QString &event, QJsonObject args = {});
	[[nodiscard]] int overlayAt(QPoint point) const;
	[[nodiscard]] QRect overlayRect(const Overlay &overlay) const;

	const not_null<QWidget*> _anchor;
	const QString _name;
	std::vector<Overlay> _overlays;
	std::optional<QColor> _background;
	QPointer<QGraphicsOpacityEffect> _effect;
	bool _hidden = false;
	bool _ownsTransparency = false;
	base::Timer _resize;

};

AnchorLayer::AnchorLayer(not_null<QWidget*> anchor, QString name)
: RpWidget(anchor)
, _anchor(anchor)
, _name(std::move(name))
, _resize([=] {
	send(u"resize"_q, {
		{ u"width"_q, _anchor->width() },
		{ u"height"_q, _anchor->height() },
	});
}) {
	setObjectName(u"moegram_anchor_layer"_q);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setGeometry(anchor->rect());
	anchor->installEventFilter(this);
	show();
	raise();
}

void AnchorLayer::send(const QString &event, QJsonObject args) {
	args.insert(u"anchor"_q, _name);
	args.insert(u"event"_q, event);
	for (const auto &[pluginId, state] : Anchors().states[_name]) {
		const auto callback = state.value(u"events"_q).toObject().value(event);
		InvokePluginCallback(pluginId, callback, args);
	}
}

void AnchorLayer::apply() {
	const auto &states = Anchors().states[_name];
	_overlays.clear();
	_background = std::nullopt;
	auto opacity = 1.;
	auto hidden = false;
	for (const auto &[pluginId, state] : states) {
		if (state.contains(u"opacity"_q)) {
			opacity = std::min(
				opacity,
				std::clamp(state.value(u"opacity"_q).toDouble(1.), 0., 1.));
		}
		if (state.value(u"visible"_q).toBool(true) == false) {
			hidden = true;
		}
		const auto background = state.value(u"background"_q).toObject();
		if (const auto color = ParseColor(background.value(u"color"_q))) {
			_background = color;
		}
		for (const auto &entry : state.value(u"overlays"_q).toArray()) {
			const auto object = entry.toObject();
			const auto rect = object.value(u"rect"_q).toArray();
			auto overlay = Overlay{
				.pluginId = pluginId,
				.id = object.value(u"id"_q).toInt(),
				.rect = QRect(
					rect.at(0).toInt(),
					rect.at(1).toInt(),
					rect.at(2).toInt(),
					rect.at(3).toInt()),
				.text = object.value(u"text"_q).toString().left(256),
				.fontSize = std::clamp(object.value(u"font_size"_q).toInt(0), 0, 64),
				.align = Align(object.value(u"align"_q).toString()),
				.radius = std::clamp(object.value(u"rounded"_q).toInt(0), 0, 512),
				.color = ParseColor(object.value(u"color"_q)),
				.background = ParseColor(object.value(u"background"_q)),
				.callback = object.value(u"on_click"_q),
			};
			if (const auto path = object.value(u"image"_q).toString()
					; !path.isEmpty()) {
				overlay.image = LoadImage(path);
			}
			_overlays.push_back(std::move(overlay));
		}
	}
	_hidden = hidden;
	if (hidden) {
		opacity = 0.;
	}
	if (opacity < 1.) {
		if (!_effect) {
			_effect = new QGraphicsOpacityEffect(_anchor);
			_anchor->setGraphicsEffect(_effect);
		}
		_effect->setOpacity(opacity);
	} else if (_effect) {
		_anchor->setGraphicsEffect(nullptr);
	}
	if (hidden != _ownsTransparency) {
		_ownsTransparency = hidden;
		_anchor->setAttribute(Qt::WA_TransparentForMouseEvents, hidden);
	}
	raise();
	_anchor->update();
	update();
}

QRect AnchorLayer::overlayRect(const Overlay &overlay) const {
	auto rect = overlay.rect;
	if (rect.width() <= 0) rect.setWidth(width() - rect.x());
	if (rect.height() <= 0) rect.setHeight(height() - rect.y());
	return rect;
}

int AnchorLayer::overlayAt(QPoint point) const {
	for (auto i = int(_overlays.size()); i != 0;) {
		const auto &overlay = _overlays[--i];
		if (overlay.callback.isObject() && overlayRect(overlay).contains(point)) {
			return i;
		}
	}
	return -1;
}

bool AnchorLayer::eventFilter(QObject *o, QEvent *e) {
	if (o != _anchor) {
		return false;
	}
	switch (e->type()) {
	case QEvent::Resize:
		setGeometry(_anchor->rect());
		_resize.callOnce(100);
		break;
	case QEvent::ChildAdded:
		// Keep overlays above children added later.
		crl::on_main(this, [=] { raise(); });
		break;
	case QEvent::Paint:
		if (_background) {
			// Runs right before the anchor paints itself.
			auto p = QPainter(_anchor);
			p.fillRect(_anchor->rect(), *_background);
		}
		break;
	case QEvent::MouseButtonPress: {
		// Clicks on plugin overlays don't reach the anchor itself.
		const auto mouse = static_cast<QMouseEvent*>(e);
		const auto index = (mouse->button() == Qt::LeftButton && !_hidden)
			? overlayAt(mouse->pos())
			: -1;
		if (index >= 0) {
			const auto &overlay = _overlays[index];
			InvokePluginCallback(overlay.pluginId, overlay.callback, {
				{ u"anchor"_q, _name },
				{ u"overlay"_q, overlay.id },
				{ u"x"_q, mouse->pos().x() },
				{ u"y"_q, mouse->pos().y() },
			});
			return true;
		}
	} break;
	case QEvent::MouseButtonRelease: {
		const auto mouse = static_cast<QMouseEvent*>(e);
		if (mouse->button() == Qt::LeftButton) {
			send(u"click"_q, {
				{ u"x"_q, mouse->pos().x() },
				{ u"y"_q, mouse->pos().y() },
			});
		}
	} break;
	case QEvent::Enter: send(u"enter"_q); break;
	case QEvent::Leave: send(u"leave"_q); break;
	default: break;
	}
	return false;
}

void AnchorLayer::paintEvent(QPaintEvent *e) {
	if (_overlays.empty()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	for (const auto &overlay : _overlays) {
		const auto rect = overlayRect(overlay);
		if (overlay.background) {
			p.setPen(Qt::NoPen);
			p.setBrush(*overlay.background);
			p.drawRoundedRect(rect, overlay.radius, overlay.radius);
		}
		if (!overlay.image.isNull()) {
			if (overlay.radius > 0) {
				auto path = QPainterPath();
				path.addRoundedRect(rect, overlay.radius, overlay.radius);
				p.save();
				p.setClipPath(path);
				p.drawImage(rect, overlay.image);
				p.restore();
			} else {
				p.drawImage(rect, overlay.image);
			}
		}
		if (!overlay.text.isEmpty()) {
			auto font = st::normalFont->f;
			if (overlay.fontSize > 0) {
				font.setPixelSize(style::ConvertScale(overlay.fontSize));
			}
			p.setFont(font);
			p.setPen(overlay.color.value_or(st::windowFg->c));
			p.drawText(rect, overlay.align | Qt::TextWordWrap, overlay.text);
		}
	}
}

AnchorLayer *FindLayer(not_null<QWidget*> widget, bool create, const QString &name) {
	for (const auto child : widget->children()) {
		if (const auto layer = dynamic_cast<AnchorLayer*>(child)) {
			return layer;
		}
	}
	return create ? new AnchorLayer(widget, name) : nullptr;
}

} // namespace

void RegisterAnchor(not_null<QWidget*> widget, const QString &name) {
	auto &list = Anchors().widgets[name];
	list.erase(ranges::remove(list, nullptr), end(list));
	list.push_back(widget.get());
	if (!Anchors().states[name].empty()) {
		FindLayer(widget, true, name)->apply();
	}
}

void ApplyAnchor(
		const QString &pluginId,
		const QString &name,
		const std::optional<QJsonValue> &value) {
	auto &states = Anchors().states[name];
	if (value) {
		states[pluginId] = value->toObject();
	} else {
		states.erase(pluginId);
	}
	auto &list = Anchors().widgets[name];
	list.erase(ranges::remove(list, nullptr), end(list));
	for (const auto &widget : list) {
		if (const auto layer = FindLayer(widget, !states.empty(), name)) {
			layer->apply();
		}
	}
}

}
