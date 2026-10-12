#pragma once

#include "ui/rp_widget.h"

namespace MoeGram::Ui {

// Plugin additions to the chat top bar: badges and buttons on the right,
// an optional status line and background tint. Merged from every plugin.
class ChatHeaderDecor final : public ::Ui::RpWidget {
public:
	explicit ChatHeaderDecor(QWidget *parent);

	[[nodiscard]] const QString &subtitle() const {
		return _subtitle;
	}
	[[nodiscard]] std::optional<QColor> background() const {
		return _background;
	}
	[[nodiscard]] float64 opacity() const {
		return _opacity;
	}
	// Fires when the parent should relayout and repaint.
	[[nodiscard]] rpl::producer<> changes() const {
		return _changes.events();
	}

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Part {
		QString pluginId;
		QString text;
		QJsonValue callback;
		std::optional<QColor> color;
		std::optional<QColor> bg;
		QRect rect;
	};

	void refresh();
	[[nodiscard]] int partAt(QPoint point) const;

	std::vector<Part> _parts;
	QString _subtitle;
	std::optional<QColor> _background;
	float64 _opacity = 1.;
	int _over = -1;
	rpl::event_stream<> _changes;

};

}
