#include "moegram/ui/plugin_particles.h"

#include "moegram/ui/plugin_ui.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_basic.h"

#include <QtCore/QJsonObject>
#include <QtCore/QPointer>

#include <numbers>
#include <random>

namespace MoeGram::Ui {
namespace {

constexpr auto kMaxFrameTime = crl::time(50);

enum class Mode {
	Snow,
	Sakura,
	Matrix,
	Rain,
	Leaves,
};

struct Config {
	Mode mode = Mode::Snow;
	int count = 80;
	float64 opacity = 0.8;
	float64 speed = 1.;
	float64 rotation = 1.;
	std::optional<QColor> color;
};

struct Particle {
	float64 x = 0.;
	float64 y = 0.;
	float64 vx = 0.;
	float64 vy = 0.;
	float64 size = 1.;
	float64 angle = 0.;
	float64 spin = 0.;
	float64 phase = 0.;
	float64 alpha = 1.;
	int variant = 0;
	int glyph = 0;
};

Mode ParseMode(const QString &value) {
	return (value == u"sakura"_q) ? Mode::Sakura
		: (value == u"matrix"_q) ? Mode::Matrix
		: (value == u"rain"_q) ? Mode::Rain
		: (value == u"leaves"_q) ? Mode::Leaves
		: Mode::Snow;
}

QPainterPath PetalPath() {
	auto result = QPainterPath();
	result.moveTo(0., -1.);
	result.cubicTo(0.9, -0.9, 0.75, 0.55, 0., 1.);
	result.cubicTo(-0.75, 0.55, -0.9, -0.9, 0., -1.);
	return result;
}

QPainterPath LeafPath() {
	auto result = QPainterPath();
	result.moveTo(0., -1.);
	result.quadTo(0.85, 0., 0., 1.);
	result.quadTo(-0.85, 0., 0., -1.);
	return result;
}

const QString &Glyphs() {
	static const auto result = u"0123456789ABCDEFアイウエオカキクケコサシスセソタチツテトナニヌネノ"_q;
	return result;
}

int RandomGlyph(std::mt19937 &random) {
	return std::uniform_int_distribution<int>(
		0,
		int(Glyphs().size()) - 1)(random);
}

class ParticlesLayer final : public ::Ui::RpWidget {
public:
	ParticlesLayer(not_null<QWidget*> target, QString area);

protected:
	void paintEvent(QPaintEvent *e) override;
	void showEvent(QShowEvent *e) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void refresh();
	void reseed();
	void respawn(Particle &particle, bool anywhere);
	void maybeStart();
	bool step(crl::time now);
	[[nodiscard]] bool paused() const;
	void paintParticle(QPainter &p, const Particle &particle);

	const not_null<QWidget*> _target;
	const QString _area;
	std::optional<Config> _config;
	std::vector<Particle> _particles;
	std::mt19937 _random;
	::Ui::Animations::Basic _animation;
	crl::time _last = 0;
	QPointer<QWidget> _window;
	QPainterPath _petal;
	QPainterPath _leaf;

};

ParticlesLayer::ParticlesLayer(not_null<QWidget*> target, QString area)
: RpWidget(target)
, _target(target)
, _area(std::move(area))
, _random(std::random_device()())
, _animation([=](crl::time now) { return step(now); })
, _petal(PetalPath())
, _leaf(LeafPath()) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setGeometry(target->rect());
	target->installEventFilter(this);
	hide();
	PluginValueChanges(
		u"effect:particles"_q
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
	refresh();
}

void ParticlesLayer::refresh() {
	auto config = std::optional<Config>();
	for (const auto &[pluginId, value] : PluginValues(u"effect:particles"_q)) {
		const auto spec = value.toObject();
		if (spec.value(u"area"_q).toString(u"window"_q) != _area) {
			continue;
		}
		config = Config{
			.mode = ParseMode(spec.value(u"mode"_q).toString()),
			.count = std::clamp(spec.value(u"count"_q).toInt(80), 10, 400),
			.opacity = std::clamp(spec.value(u"opacity"_q).toDouble(0.8), 0., 1.),
			.speed = std::clamp(spec.value(u"speed"_q).toDouble(1.), 0.1, 4.),
			.rotation = std::clamp(spec.value(u"rotation"_q).toDouble(1.), 0., 4.),
			.color = ParseColor(spec.value(u"color"_q)),
		};
	}
	if (!config) {
		_config = std::nullopt;
		_animation.stop();
		_particles.clear();
		hide();
		return;
	}
	const auto reset = !_config
		|| _config->mode != config->mode
		|| _config->count != config->count;
	_config = config;
	if (reset) {
		reseed();
	}
	show();
	raise();
	maybeStart();
}

void ParticlesLayer::reseed() {
	_particles.resize(_config->count);
	for (auto &particle : _particles) {
		respawn(particle, true);
	}
}

void ParticlesLayer::respawn(Particle &particle, bool anywhere) {
	auto uniform = [&](float64 from, float64 till) {
		return std::uniform_real_distribution<float64>(from, till)(_random);
	};
	const auto w = std::max(width(), 1);
	const auto h = std::max(height(), 1);
	const auto scale = style::Scale() / 100.;
	particle.x = uniform(0., w);
	particle.y = anywhere ? uniform(0., h) : uniform(-h * 0.1, 0.) - 20. * scale;
	particle.phase = uniform(0., 2 * std::numbers::pi);
	particle.angle = uniform(0., 360.);
	particle.spin = uniform(-90., 90.);
	particle.alpha = uniform(0.5, 1.);
	particle.variant = std::uniform_int_distribution<int>(0, 3)(_random);
	switch (_config->mode) {
	case Mode::Snow:
		particle.size = uniform(1.5, 4.) * scale;
		particle.vy = uniform(35., 80.) * scale;
		particle.vx = uniform(-12., 12.) * scale;
		break;
	case Mode::Sakura:
		particle.size = uniform(4., 7.) * scale;
		particle.vy = uniform(35., 70.) * scale;
		particle.vx = uniform(10., 35.) * scale;
		break;
	case Mode::Leaves:
		particle.size = uniform(5., 9.) * scale;
		particle.vy = uniform(45., 85.) * scale;
		particle.vx = uniform(-20., 20.) * scale;
		break;
	case Mode::Rain:
		particle.size = uniform(10., 22.) * scale;
		particle.vy = uniform(550., 850.) * scale;
		particle.vx = -particle.vy * 0.08;
		particle.alpha = uniform(0.3, 0.7);
		break;
	case Mode::Matrix:
		particle.size = std::round(uniform(11., 17.)) * scale;
		particle.x = std::round(particle.x / (14. * scale)) * 14. * scale;
		particle.vy = uniform(90., 220.) * scale;
		particle.vx = 0.;
		particle.glyph = RandomGlyph(_random);
		break;
	}
}

bool ParticlesLayer::paused() const {
	return !isVisible() || (_window && _window->isMinimized());
}

void ParticlesLayer::maybeStart() {
	if (!_window) {
		_window = window();
		if (_window && _window != this) {
			_window->installEventFilter(this);
		}
	}
	if (_config && !paused() && !_animation.animating()) {
		_last = crl::now();
		_animation.start();
	}
}

bool ParticlesLayer::step(crl::time now) {
	if (!_config || paused()) {
		return false;
	}
	const auto dt = std::min(now - _last, kMaxFrameTime) / 1000.;
	_last = now;
	const auto speed = _config->speed;
	const auto spin = _config->rotation;
	const auto h = height();
	const auto w = width();
	for (auto &particle : _particles) {
		particle.phase += dt * 1.5;
		particle.y += particle.vy * speed * dt;
		particle.x += (particle.vx + std::sin(particle.phase) * particle.size * 4.)
			* speed * dt;
		particle.angle += particle.spin * spin * dt;
		if (_config->mode == Mode::Matrix
			&& std::uniform_int_distribution<int>(0, 30)(_random) == 0) {
			particle.glyph = RandomGlyph(_random);
		}
		const auto margin = particle.size * 6.;
		if (particle.y > h + margin
			|| particle.x < -margin
			|| particle.x > w + margin) {
			respawn(particle, false);
		}
	}
	update();
	return true;
}

void ParticlesLayer::paintParticle(QPainter &p, const Particle &particle) {
	const auto base = _config->opacity * particle.alpha;
	p.setOpacity(base);
	switch (_config->mode) {
	case Mode::Snow: {
		p.setPen(Qt::NoPen);
		p.setBrush(_config->color.value_or(QColor(255, 255, 255)));
		p.drawEllipse(
			QPointF(particle.x, particle.y),
			particle.size,
			particle.size);
	} break;
	case Mode::Rain: {
		auto pen = QPen(_config->color.value_or(QColor(158, 201, 255)));
		pen.setWidthF(std::max(style::Scale() / 100., 1.));
		pen.setCapStyle(Qt::RoundCap);
		p.setPen(pen);
		const auto k = particle.size / particle.vy;
		p.drawLine(
			QPointF(particle.x, particle.y),
			QPointF(
				particle.x - particle.vx * k,
				particle.y - particle.size));
	} break;
	case Mode::Matrix: {
		auto font = st::normalFont->f;
		font.setPixelSize(int(particle.size));
		font.setFamily(u"monospace"_q);
		p.setFont(font);
		const auto color = _config->color.value_or(QColor(60, 255, 120));
		for (auto i = 0; i != 4; ++i) {
			p.setOpacity(base * (1. - i * 0.25));
			p.setPen(i ? color : color.lighter(160));
			const auto &glyphs = Glyphs();
			p.drawText(
				QPointF(particle.x, particle.y - i * particle.size * 1.1),
				QString(glyphs[(particle.glyph + i * 7) % glyphs.size()]));
		}
	} break;
	case Mode::Sakura:
	case Mode::Leaves: {
		const auto leaves = (_config->mode == Mode::Leaves);
		static const auto petals = std::array{
			QColor(255, 183, 197),
			QColor(255, 200, 214),
			QColor(250, 160, 185),
			QColor(255, 220, 228),
		};
		static const auto foliage = std::array{
			QColor(224, 123, 57),
			QColor(201, 71, 43),
			QColor(227, 167, 47),
			QColor(140, 90, 43),
		};
		const auto fill = _config->color.value_or(leaves
			? foliage[particle.variant]
			: petals[particle.variant]);
		p.save();
		p.translate(particle.x, particle.y);
		p.rotate(particle.angle);
		p.scale(
			particle.size * (0.35 + 0.65 * std::abs(std::cos(particle.phase))),
			particle.size * (leaves ? 1.4 : 1.));
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawPath(leaves ? _leaf : _petal);
		if (leaves) {
			auto pen = QPen(fill.darker(140));
			pen.setWidthF(0.12);
			p.setPen(pen);
			p.drawLine(QPointF(0., -0.9), QPointF(0., 0.9));
		}
		p.restore();
	} break;
	}
}

void ParticlesLayer::paintEvent(QPaintEvent *e) {
	if (!_config || _particles.empty()) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	for (const auto &particle : _particles) {
		paintParticle(p, particle);
	}
}

void ParticlesLayer::showEvent(QShowEvent *e) {
	RpWidget::showEvent(e);
	maybeStart();
}

bool ParticlesLayer::eventFilter(QObject *o, QEvent *e) {
	if (o == _target) {
		if (e->type() == QEvent::Resize) {
			const auto was = size();
			setGeometry(_target->rect());
			if (_config && was.isEmpty() && !size().isEmpty()) {
				reseed();
			}
		} else if (e->type() == QEvent::ChildAdded && _config) {
			crl::on_main(this, [=] { raise(); });
		}
	} else if (o == _window.data()) {
		if (e->type() == QEvent::WindowStateChange
			|| e->type() == QEvent::Show) {
			crl::on_main(this, [=] { maybeStart(); });
		}
	}
	return false;
}

} // namespace

void AttachParticles(not_null<QWidget*> target, const QString &area) {
	::Ui::CreateChild<ParticlesLayer>(target.get(), area);
}

}
