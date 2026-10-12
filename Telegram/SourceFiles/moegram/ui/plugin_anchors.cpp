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
#include <QtGui/QMovie>
#include <QtWidgets/QApplication>
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
	QString animation;
	QJsonValue callback;
	QJsonValue drag;
	bool draggable = false;
};

bool IsAnimated(const QString &path) {
	const auto info = QFileInfo(path);
	if (!info.isFile() || info.size() > kMaxImageBytes) {
		return false;
	}
	auto reader = QImageReader(path);
	return reader.supportsAnimation() && reader.imageCount() != 1;
}

// Takes clicks and drags for one overlay; the paint layer itself is mouse transparent.
class OverlayHit final : public ::Ui::RpWidget {
public:
	using Handler = Fn<void(QEvent::Type, QPoint)>;
	OverlayHit(not_null<QWidget*> parent, Handler handler)
	: RpWidget(parent)
	, _handler(std::move(handler)) {
	}

protected:
	void mousePressEvent(QMouseEvent *e) override {
		handle(e);
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		handle(e);
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		handle(e);
	}

private:
	void handle(QMouseEvent *e) {
		if (e->button() == Qt::LeftButton
			|| (e->buttons() & Qt::LeftButton)) {
			_handler(e->type(), e->globalPosition().toPoint());
		}
	}

	const Handler _handler;

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
	void showEvent(QShowEvent *e) override;
	void hideEvent(QHideEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	struct Drag {
		int index = -1;
		QPoint start;
		QPoint origin;
		bool moved = false;
	};

	void send(const QString &event, QJsonObject args = {});
	[[nodiscard]] QRect overlayRect(const Overlay &overlay) const;
	void refreshMovies();
	void refreshHits();
	void raiseAll();
	void hitEvent(int index, QEvent::Type type, QPoint global);

	const not_null<QWidget*> _anchor;
	const QString _name;
	std::vector<Overlay> _overlays;
	std::map<QString, std::unique_ptr<QMovie>> _movies;
	std::vector<base::unique_qptr<OverlayHit>> _hits;
	Drag _drag;
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
				.drag = object.value(u"on_drag"_q),
				.draggable = object.value(u"draggable"_q).toBool(),
			};
			if (const auto path = object.value(u"image"_q).toString()
					; !path.isEmpty()) {
				if (IsAnimated(path)) {
					overlay.animation = path;
				} else {
					overlay.image = LoadImage(path);
				}
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
	_drag = Drag();
	refreshMovies();
	refreshHits();
	raiseAll();
	_anchor->update();
	update();
}

QRect AnchorLayer::overlayRect(const Overlay &overlay) const {
	auto rect = overlay.rect;
	if (rect.width() <= 0) rect.setWidth(width() - rect.x());
	if (rect.height() <= 0) rect.setHeight(height() - rect.y());
	return rect;
}

void AnchorLayer::refreshMovies() {
	auto used = base::flat_set<QString>();
	for (const auto &overlay : _overlays) {
		if (!overlay.animation.isEmpty()) {
			used.emplace(overlay.animation);
		}
	}
	for (auto i = begin(_movies); i != end(_movies);) {
		if (!used.contains(i->first)) {
			i = _movies.erase(i);
		} else {
			++i;
		}
	}
	for (const auto &path : used) {
		if (_movies.contains(path)) {
			continue;
		}
		auto movie = std::make_unique<QMovie>(path);
		QObject::connect(movie.get(), &QMovie::frameChanged, this, [=] {
			update();
		});
		movie->start();
		movie->setPaused(!isVisible());
		_movies.emplace(path, std::move(movie));
	}
}

void AnchorLayer::refreshHits() {
	_hits.clear();
	_hits.resize(_overlays.size());
	for (auto i = 0; i != int(_overlays.size()); ++i) {
		const auto &overlay = _overlays[i];
		if (_hidden || (!overlay.draggable && !overlay.callback.isObject())) {
			continue;
		}
		auto hit = base::make_unique_q<OverlayHit>(
			_anchor,
			[=](QEvent::Type type, QPoint global) {
				hitEvent(i, type, global);
			});
		hit->setCursor(overlay.draggable
			? Qt::OpenHandCursor
			: Qt::PointingHandCursor);
		hit->setGeometry(overlayRect(overlay));
		hit->show();
		_hits[i] = std::move(hit);
	}
}

void AnchorLayer::raiseAll() {
	raise();
	for (const auto &hit : _hits) {
		if (hit) {
			hit->raise();
		}
	}
}

void AnchorLayer::hitEvent(int index, QEvent::Type type, QPoint global) {
	if (index < 0 || index >= int(_overlays.size()) || !_hits[index]) {
		return;
	}
	auto &overlay = _overlays[index];
	const auto hit = _hits[index].get();
	switch (type) {
	case QEvent::MouseButtonPress:
		_drag = Drag{
			.index = index,
			.start = global,
			.origin = overlay.rect.topLeft(),
		};
		break;
	case QEvent::MouseMove: {
		if (_drag.index != index || !overlay.draggable) {
			break;
		}
		const auto delta = global - _drag.start;
		if (!_drag.moved
			&& delta.manhattanLength() < QApplication::startDragDistance()) {
			break;
		}
		if (!_drag.moved) {
			_drag.moved = true;
			hit->setCursor(Qt::ClosedHandCursor);
		}
		const auto size = overlayRect(overlay).size();
		const auto position = QPoint(
			std::clamp(
				_drag.origin.x() + delta.x(),
				0,
				std::max(width() - size.width(), 0)),
			std::clamp(
				_drag.origin.y() + delta.y(),
				0,
				std::max(height() - size.height(), 0)));
		overlay.rect.moveTopLeft(position);
		hit->move(position);
		update();
	} break;
	case QEvent::MouseButtonRelease: {
		if (_drag.index != index) {
			break;
		}
		const auto moved = _drag.moved;
		_drag = Drag();
		if (moved) {
			hit->setCursor(Qt::OpenHandCursor);
			InvokePluginCallback(overlay.pluginId, overlay.drag, {
				{ u"anchor"_q, _name },
				{ u"overlay"_q, overlay.id },
				{ u"x"_q, overlay.rect.x() },
				{ u"y"_q, overlay.rect.y() },
			});
		} else if (overlay.callback.isObject()) {
			const auto local = _anchor->mapFromGlobal(global);
			InvokePluginCallback(overlay.pluginId, overlay.callback, {
				{ u"anchor"_q, _name },
				{ u"overlay"_q, overlay.id },
				{ u"x"_q, local.x() },
				{ u"y"_q, local.y() },
			});
		}
	} break;
	default: break;
	}
}

void AnchorLayer::showEvent(QShowEvent *e) {
	RpWidget::showEvent(e);
	for (const auto &[path, movie] : _movies) {
		movie->setPaused(false);
	}
}

void AnchorLayer::hideEvent(QHideEvent *e) {
	RpWidget::hideEvent(e);
	for (const auto &[path, movie] : _movies) {
		movie->setPaused(true);
	}
}

bool AnchorLayer::eventFilter(QObject *o, QEvent *e) {
	if (o != _anchor) {
		return false;
	}
	switch (e->type()) {
	case QEvent::Resize:
		setGeometry(_anchor->rect());
		for (auto i = 0; i != int(_hits.size()); ++i) {
			if (_hits[i]) {
				_hits[i]->setGeometry(overlayRect(_overlays[i]));
			}
		}
		_resize.callOnce(100);
		break;
	case QEvent::ChildAdded:
		// Keep overlays above children added later.
		if (!dynamic_cast<OverlayHit*>(static_cast<QChildEvent*>(e)->child())) {
			crl::on_main(this, [=] { raiseAll(); });
		}
		break;
	case QEvent::Paint:
		if (_background) {
			// Runs right before the anchor paints itself.
			auto p = QPainter(_anchor);
			p.fillRect(_anchor->rect(), *_background);
		}
		break;
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
		const auto movie = overlay.animation.isEmpty()
			? nullptr
			: _movies[overlay.animation].get();
		const auto image = movie ? movie->currentImage() : overlay.image;
		if (!image.isNull()) {
			if (overlay.radius > 0) {
				auto path = QPainterPath();
				path.addRoundedRect(rect, overlay.radius, overlay.radius);
				p.save();
				p.setClipPath(path);
				p.drawImage(rect, image);
				p.restore();
			} else {
				p.drawImage(rect, image);
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
