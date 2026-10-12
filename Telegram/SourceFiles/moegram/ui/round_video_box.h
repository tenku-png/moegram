#pragma once

#include "base/basic_types.h"
#include "menu/menu_send.h"

namespace Api {
struct SendAction;
} // namespace Api

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace MoeGram::Ui {

struct RoundCandidate {
	QString path;
	QImage frame;
	crl::time duration = 0;
};

// Puts "Send as video message" on top of the send menu while `candidate` has a video.
void SetupSendAsRound(
	not_null<::Ui::RpWidget*> button,
	std::shared_ptr<ChatHelpers::Show> show,
	Fn<SendMenu::Details()> details,
	Fn<void(SendMenu::Action, SendMenu::Details)> action,
	Fn<std::optional<RoundCandidate>()> candidate,
	Fn<Api::SendAction()> sendAction,
	Fn<void()> sent,
	const style::PopupMenu *menuSt = nullptr,
	const style::ComposeIcons *iconsSt = nullptr);

}
