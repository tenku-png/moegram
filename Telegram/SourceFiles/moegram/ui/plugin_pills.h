#pragma once

#include "plugins/plugin_host.h"
#include "ui/rp_widget.h"

namespace MoeGram::Ui {

// Called on the main thread by the plugin host.
void SetPluginPill(QString id, std::optional<Plugins::PillState> state);

class PluginPills final : public ::Ui::RpWidget {
public:
	explicit PluginPills(QWidget *parent);

	[[nodiscard]] rpl::producer<int> widthValueWanted() const;
	void setPillHeight(int height);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Pill {
		QString id;
		QString text;
		bool clickable = false;
		QRect rect;
	};

	void refresh();
	[[nodiscard]] int pillAt(QPoint point) const;

	std::vector<Pill> _pills;
	rpl::variable<int> _wanted = 0;
	int _height = 0;
	int _over = -1;

};

}
