#include "moegram/ui/round_video_box.h"

#include "apiwrap.h"
#include "base/event_filter.h"
#include "base/unique_qptr.h"
#include "chat_helpers/compose/compose_show.h"
#include "main/main_session.h"
#include "moegram/media/round_video_converter.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QMouseEvent>

namespace MoeGram::Ui {
namespace {

constexpr auto kMaxLength = 60 * crl::time(1000);
constexpr auto kMinSize = 30;
constexpr auto kMaxStartSteps = 240;

struct CropState {
	QPointF center = QPointF(0.5, 0.5);
	int size = 100;
	crl::time from = 0;
	crl::time length = kMaxLength;
};

QString FormatTime(crl::time ms) {
	const auto seconds = ms / 1000;
	return u"%1:%2"_q.arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
}

void AddSlider(
		not_null<::Ui::GenericBox*> box,
		int steps,
		int current,
		Fn<QString(int)> format,
		Fn<void(int)> changed) {
	auto slider = Settings::MakeSliderWithLabel(
		box,
		st::autoDownloadLimitSlider,
		st::settingsScaleLabel,
		0,
		st::settingsScaleLabel.style.font->width(u"Length 00:00"_q),
		true);
	box->addRow(std::move(slider.widget), st::boxRowPadding);
	const auto label = slider.label;
	label->setText(format(current));
	slider.slider->setPseudoDiscrete(
		steps,
		[](int index) { return index; },
		current,
		[=](int index) {
			label->setText(format(index));
			changed(index);
		});
}

void ShowRoundBox(
		std::shared_ptr<ChatHelpers::Show> show,
		RoundCandidate candidate,
		Fn<void(Media::RoundVideoSource)> done) {
	show->show(Box([=](not_null<::Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Video message"_q));
		box->setWidth(st::boxWideWidth);
		const auto state = box->lifetime().make_state<CropState>();
		const auto duration = std::max(candidate.duration, crl::time(1000));
		state->length = std::min(duration, kMaxLength);

		auto frame = candidate.frame;
		if (frame.isNull()) {
			frame = QImage(QSize(16, 9), QImage::Format_ARGB32_Premultiplied);
			frame.fill(Qt::black);
		}
		const auto width = st::boxWideWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		const auto height = std::min(
			width * frame.height() / std::max(frame.width(), 1),
			width);
		const auto preview = box->addRow(
			object_ptr<::Ui::RpWidget>(box),
			st::boxRowPadding);
		preview->resize(width, height);
		preview->setCursor(Qt::OpenHandCursor);
		const auto shown = [=] {
			const auto size = frame.size().scaled(
				preview->size(),
				Qt::KeepAspectRatio);
			return QRect(
				(preview->width() - size.width()) / 2,
				(preview->height() - size.height()) / 2,
				size.width(),
				size.height());
		};
		// Circle diameter in frame pixels and its center kept inside the frame.
		const auto diameter = [=] {
			return std::min(frame.width(), frame.height()) * state->size / 100.;
		};
		const auto clampCenter = [=] {
			const auto rx = diameter() / 2. / frame.width();
			const auto ry = diameter() / 2. / frame.height();
			state->center = QPointF(
				std::clamp(state->center.x(), rx, 1. - rx),
				std::clamp(state->center.y(), ry, 1. - ry));
		};
		preview->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(preview);
			auto hq = PainterHighQualityEnabler(p);
			const auto rect = shown();
			p.drawImage(rect, frame);
			const auto scale = rect.width() / double(frame.width());
			const auto radius = diameter() / 2. * scale;
			const auto center = QPointF(
				rect.x() + state->center.x() * rect.width(),
				rect.y() + state->center.y() * rect.height());
			auto outside = QPainterPath();
			outside.addRect(rect);
			auto circle = QPainterPath();
			circle.addEllipse(center, radius, radius);
			p.fillPath(outside.subtracted(circle), QColor(0, 0, 0, 140));
			auto pen = QPen(QColor(255, 255, 255, 220));
			pen.setWidthF(style::ConvertScaleExact(2.));
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(center, radius, radius);
		}, preview->lifetime());

		const auto dragging = preview->lifetime().make_state<std::optional<QPoint>>();
		preview->events() | rpl::on_next([=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type != QEvent::MouseButtonPress
				&& type != QEvent::MouseMove
				&& type != QEvent::MouseButtonRelease) {
				return;
			}
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto position = mouse->pos();
			if (type == QEvent::MouseButtonPress
				&& mouse->button() == Qt::LeftButton) {
				*dragging = position;
				preview->setCursor(Qt::ClosedHandCursor);
			} else if (type == QEvent::MouseMove && *dragging) {
				const auto rect = shown();
				const auto delta = position - **dragging;
				*dragging = position;
				state->center += QPointF(
					delta.x() / double(std::max(rect.width(), 1)),
					delta.y() / double(std::max(rect.height(), 1)));
				clampCenter();
				preview->update();
			} else if (type == QEvent::MouseButtonRelease) {
				*dragging = std::nullopt;
				preview->setCursor(Qt::OpenHandCursor);
			}
		}, preview->lifetime());

		AddSlider(box, 100 - kMinSize + 1, 100 - kMinSize, [](int index) {
			return u"Size %1%"_q.arg(kMinSize + index);
		}, [=](int index) {
			state->size = kMinSize + index;
			clampCenter();
			preview->update();
		});
		const auto startStep = std::max(
			crl::time(500),
			duration / kMaxStartSteps);
		const auto startSteps = int(std::max(
			(duration - crl::time(1000)) / startStep,
			crl::time(1))) + 1;
		AddSlider(box, startSteps, 0, [=](int index) {
			return u"Start "_q + FormatTime(index * startStep);
		}, [=](int index) {
			state->from = index * startStep;
		});
		const auto lengthSteps = int(state->length / 1000);
		if (lengthSteps > 1) {
			AddSlider(box, lengthSteps, lengthSteps - 1, [](int index) {
				return u"Length "_q + FormatTime((index + 1) * 1000);
			}, [=](int index) {
				state->length = (index + 1) * crl::time(1000);
			});
		}

		box->addButton(rpl::single(u"Send"_q), [=] {
			const auto d = diameter();
			const auto crop = QRectF(
				state->center.x() - d / 2. / frame.width(),
				state->center.y() - d / 2. / frame.height(),
				d / frame.width(),
				d / frame.height());
			const auto from = std::min(state->from, duration - crl::time(500));
			box->closeBox();
			done({
				.path = candidate.path,
				.crop = crop,
				.from = from,
				.till = std::min(from + state->length, duration),
			});
		});
		box->addButton(rpl::single(u"Cancel"_q), [=] { box->closeBox(); });
	}));
}

void SendRound(
		std::shared_ptr<ChatHelpers::Show> show,
		Api::SendAction action,
		Media::RoundVideoSource source) {
	show->showToast(u"Preparing video message…"_q);
	const auto weak = base::make_weak(&show->session());
	crl::async([=] {
		auto video = Media::ConvertToRoundVideo(source);
		crl::on_main([=, video = std::move(video)] {
			const auto session = weak.get();
			if (!session) {
				return;
			} else if (video.content.isEmpty()) {
				show->showToast(u"Could not make a video message from this file."_q);
				return;
			}
			session->api().sendVoiceMessage(
				video.content,
				VoiceWaveform(),
				video.duration,
				true,
				action);
		});
	});
}

} // namespace

void SetupSendAsRound(
		not_null<::Ui::RpWidget*> button,
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<SendMenu::Details()> details,
		Fn<void(SendMenu::Action, SendMenu::Details)> action,
		Fn<std::optional<RoundCandidate>()> candidate,
		Fn<Api::SendAction()> sendAction,
		Fn<void()> sent,
		const style::PopupMenu *menuSt,
		const style::ComposeIcons *iconsSt) {
	const auto menu = std::make_shared<base::unique_qptr<::Ui::PopupMenu>>();
	const auto open = [=](RoundCandidate video) {
		ShowRoundBox(show, std::move(video), [=](Media::RoundVideoSource source) {
			SendRound(show, sendAction(), std::move(source));
			sent();
		});
	};
	base::install_event_filter(button, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ContextMenu) {
			return base::EventFilterResult::Continue;
		}
		auto video = candidate();
		if (!video) {
			return base::EventFilterResult::Continue;
		}
		*menu = base::make_unique_q<::Ui::PopupMenu>(
			button,
			menuSt ? *menuSt : st::popupMenuWithIcons);
		(*menu)->addAction(u"Send as video message"_q, [=] {
			open(*video);
		}, &st::menuIconVideoChat);
		const auto result = SendMenu::FillSendMenu(
			menu->get(),
			show,
			details(),
			action,
			iconsSt);
		if (result == SendMenu::FillMenuResult::Prepared) {
			(*menu)->popupPrepared();
		} else {
			(*menu)->popup(QCursor::pos());
		}
		return base::EventFilterResult::Cancel;
	});
}

}
