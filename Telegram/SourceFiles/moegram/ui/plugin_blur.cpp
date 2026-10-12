#include "moegram/ui/plugin_blur.h"

#include "base/timer.h"
#include "moegram/ui/plugin_ui.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_basic.h"

#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

namespace MoeGram::Ui {
namespace {

// Blur works on a 1/4 sized copy: 16x fewer pixels, and the smooth
// upscale adds blur for free.
constexpr auto kScale = 4;
constexpr auto kFrameMs = crl::time(33);

class EdgeBlur final : public QObject {
public:
	EdgeBlur(
		not_null<QWidget*> target,
		QString key,
		Fn<QWidget*()> source,
		BlurEdge edge);

	[[nodiscard]] bool paint(
		QPainter &p,
		QRect rect,
		const style::color &bg);

protected:
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void watch(QWidget *source);
	void dirty();

	const not_null<QWidget*> _target;
	const QString _key;
	const Fn<QWidget*()> _source;
	const BlurEdge _edge;
	QPointer<QWidget> _watched;
	QImage _cache;
	QRect _rect;
	bool _dirty = true;
	bool _rendering = false;
	crl::time _lastFrame = 0;
	base::Timer _frame;
	rpl::lifetime _lifetime;

};

EdgeBlur::EdgeBlur(
	not_null<QWidget*> target,
	QString key,
	Fn<QWidget*()> source,
	BlurEdge edge)
: QObject(target)
, _target(target)
, _key(std::move(key))
, _source(std::move(source))
, _edge(edge)
, _frame([=] { _target->update(_rect); }) {
	setObjectName(u"moegram_blur_"_q + _key);
	PluginValueChanges(
		u"blur:"_q + _key
	) | rpl::on_next([=] {
		_cache = QImage();
		_dirty = true;
		_target->update();
	}, _lifetime);
}

void EdgeBlur::watch(QWidget *source) {
	if (_watched == source) {
		return;
	}
	if (_watched) {
		_watched->removeEventFilter(this);
	}
	_watched = source;
	if (source) {
		source->installEventFilter(this);
	}
}

bool EdgeBlur::eventFilter(QObject *o, QEvent *e) {
	if (o == _watched
		&& !_rendering
		&& (e->type() == QEvent::Paint
			|| e->type() == QEvent::Move
			|| e->type() == QEvent::Resize)) {
		dirty();
	}
	return false;
}

void EdgeBlur::dirty() {
	if (_dirty) {
		return;
	}
	_dirty = true;
	// Throttle to ~30 fps, list repaints can come every frame.
	const auto wait = _lastFrame + kFrameMs - crl::now();
	if (wait <= 0) {
		_target->update(_rect);
	} else if (!_frame.isActive()) {
		_frame.callOnce(wait);
	}
}

bool EdgeBlur::paint(QPainter &p, QRect rect, const style::color &bg) {
	const auto spec = LookupBlur(_key);
	const auto source = spec ? _source() : nullptr;
	watch(source);
	if (!spec || !source || !source->isVisible()) {
		_cache = QImage();
		return false;
	}
	_rect = rect;
	if (_dirty || _cache.isNull()) {
		const auto visible = source->visibleRegion().boundingRect();
		if (visible.isEmpty()) {
			return false;
		}
		const auto height = std::min(rect.height(), visible.height());
		const auto strip = QRect(
			visible.x(),
			(_edge == BlurEdge::Top)
				? visible.y()
				: (visible.y() + visible.height() - height),
			visible.width(),
			height);
		auto image = QImage(
			strip.size() / kScale + QSize(1, 1),
			QImage::Format_ARGB32_Premultiplied);
		image.fill(bg->c);
		{
			auto q = QPainter(&image);
			q.scale(1. / kScale, 1. / kScale);
			_rendering = true;
			source->render(
				&q,
				QPoint(),
				QRegion(strip),
				QWidget::DrawChildren);
			_rendering = false;
		}
		_cache = PrepareBackdrop(std::move(image), *spec);
		_dirty = false;
		_lastFrame = crl::now();
	}
	{
		auto hq = PainterHighQualityEnabler(p);
		p.drawImage(rect, _cache);
	}
	auto color = bg->c;
	color.setAlphaF(color.alphaF() * spec->opacity);
	p.fillRect(rect, color);
	return true;
}

class MotionBlur final : public ::Ui::RpWidget {
public:
	MotionBlur(QWidget *viewport, Fn<QWidget*()> content);

	void scrolled(int top);

protected:
	void paintEvent(QPaintEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	const Fn<QWidget*()> _content;
	QImage _frame;
	int _lastTop = 0;
	crl::time _lastTime = 0;
	float64 _speed = 0.; // px per ms, signed.
	base::Timer _stop;

};

MotionBlur::MotionBlur(QWidget *viewport, Fn<QWidget*()> content)
: RpWidget(viewport)
, _content(std::move(content))
, _stop([=] { _speed = 0.; _frame = QImage(); hide(); }) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();
	setGeometry(viewport->rect());
	viewport->installEventFilter(this);
}

bool MotionBlur::eventFilter(QObject *o, QEvent *e) {
	if (o == parent() && e->type() == QEvent::Resize) {
		setGeometry(parentWidget()->rect());
	}
	return false;
}

void MotionBlur::scrolled(int top) {
	const auto now = crl::now();
	const auto delta = top - _lastTop;
	const auto elapsed = std::max(now - _lastTime, crl::time(1));
	_lastTop = top;
	_lastTime = now;
	const auto values = PluginValues(u"effect:scroll_motion_blur"_q);
	if (values.empty() || elapsed > 200) {
		_speed = 0.;
		return;
	}
	const auto options = values.back().value.toObject();
	const auto threshold = options.value(u"threshold"_q).toDouble(1.5);
	_speed = _speed * 0.5 + (float64(delta) / elapsed) * 0.5;
	if (std::abs(_speed) < threshold) {
		_stop.callOnce(60);
		return;
	}
	const auto content = _content();
	if (!content) {
		return;
	}
	// Half resolution is enough, the result is smeared anyway.
	const auto viewport = parentWidget();
	auto image = QImage(
		viewport->size() / 2 + QSize(1, 1),
		QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		q.scale(0.5, 0.5);
		const auto origin = content->mapFrom(viewport, QPoint());
		content->render(
			&q,
			QPoint(),
			QRegion(QRect(origin, viewport->size())),
			QWidget::DrawChildren);
	}
	_frame = std::move(image);
	show();
	raise();
	update();
	_stop.callOnce(80);
}

void MotionBlur::paintEvent(QPaintEvent *e) {
	if (_frame.isNull()) {
		return;
	}
	const auto values = PluginValues(u"effect:scroll_motion_blur"_q);
	const auto strength = values.empty()
		? 1.
		: values.back().value.toObject().value(u"strength"_q).toDouble(1.);
	const auto length = std::clamp(
		std::abs(_speed) * 6. * strength,
		0.,
		height() / 6.);
	if (length < 2.) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	// A few shifted copies along the scroll direction.
	constexpr auto kSteps = 6;
	const auto direction = (_speed > 0) ? -1. : 1.;
	for (auto i = 1; i <= kSteps; ++i) {
		const auto shift = direction * length * i / kSteps;
		p.setOpacity(0.5 / i);
		p.drawImage(QRectF(QPointF(0, shift), QSizeF(size())), _frame);
	}
}

} // namespace

std::optional<BlurSpec> LookupBlur(const QString &target) {
	const auto values = PluginValues(u"blur:"_q + target);
	if (values.empty()) {
		return std::nullopt;
	}
	const auto spec = values.back().value.toObject();
	return BlurSpec{
		.radius = std::clamp(spec.value(u"radius"_q).toInt(12), 1, 64),
		.opacity = std::clamp(spec.value(u"opacity"_q).toDouble(0.85), 0., 1.),
		.tint = ParseColor(spec.value(u"tint"_q)),
	};
}

QImage PrepareBackdrop(QImage image, const BlurSpec &spec) {
	if (image.isNull()) {
		return image;
	}
	const auto radius = std::max(spec.radius / kScale, 1);
	image = Images::BlurLargeImage(std::move(image), radius);
	if (spec.tint) {
		auto p = QPainter(&image);
		p.fillRect(image.rect(), *spec.tint);
	}
	return image;
}

QImage MenuBackdrop(not_null<QWidget*> menu, QRect global) {
	const auto spec = LookupBlur(u"menu"_q);
	if (!spec || global.isEmpty()) {
		return QImage();
	}
	auto window = menu->parentWidget()
		? menu->parentWidget()->window()
		: QApplication::activeWindow();
	if (!window || window == menu->window()) {
		window = QApplication::activeWindow();
	}
	if (!window || window == menu->window()) {
		return QImage();
	}
	auto image = QImage(
		global.size() / kScale + QSize(1, 1),
		QImage::Format_ARGB32_Premultiplied);
	image.fill(st::windowBg->c);
	{
		auto p = QPainter(&image);
		p.scale(1. / kScale, 1. / kScale);
		const auto local = QRect(window->mapFromGlobal(global.topLeft()), global.size());
		window->render(&p, QPoint(), QRegion(local), QWidget::DrawChildren);
	}
	return PrepareBackdrop(std::move(image), *spec);
}

void AttachEdgeBlur(
		not_null<QWidget*> target,
		const QString &key,
		Fn<QWidget*()> source,
		BlurEdge edge) {
	new EdgeBlur(target, key, std::move(source), edge);
}

bool PaintEdgeBlur(
		not_null<QWidget*> target,
		const QString &key,
		QPainter &p,
		QRect rect,
		const style::color &bg) {
	const auto name = u"moegram_blur_"_q + key;
	for (const auto child : target->children()) {
		if (child->objectName() == name) {
			if (const auto blur = dynamic_cast<EdgeBlur*>(child)) {
				return blur->paint(p, rect, bg);
			}
		}
	}
	return false;
}

void AttachScrollMotionBlur(
		not_null<QWidget*> viewport,
		Fn<QWidget*()> content,
		rpl::producer<int> scrollTop) {
	const auto blur = ::Ui::CreateChild<MotionBlur>(
		viewport.get(),
		std::move(content));
	std::move(
		scrollTop
	) | rpl::on_next([=](int top) {
		blur->scrolled(top);
	}, blur->lifetime());
}

}
