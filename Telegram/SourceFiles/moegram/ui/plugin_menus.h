#pragma once

#include "base/basic_types.h"

class PeerData;

namespace Ui {
class VerticalLayout;
}

namespace Window {
class SessionController;
}

namespace MoeGram::Ui {

// Fills `container` with plugin DRAWER_MENU items and keeps it up to date.
void SetupMainMenuItems(
	not_null<::Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller);

using ChatMenuAdd = Fn<void(const QString &text, Fn<void()> callback)>;

// Adds plugin CHAT_ACTION_MENU items for a chat.
void AddChatMenuItems(const ChatMenuAdd &add, not_null<PeerData*> peer);

}
