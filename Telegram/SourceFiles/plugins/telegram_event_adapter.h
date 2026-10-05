#pragma once

#include "plugins/plugin_host.h"
#include "base/basic_types.h"
#include "data/data_msg_id.h"
#include "data/data_peer_id.h"
#include "scheme.h"

class HistoryItem;

namespace Main {
class Session;
}

namespace MoeGram::Plugins {

[[nodiscard]] qint64 DialogId(PeerId peer);
[[nodiscard]] bool ObserveTelegramUpdate(
	not_null<Main::Session*> session,
	const MTPUpdate &update);
void PublishModelMessage(
	not_null<Main::Session*> session,
	EventKind kind,
	const MTPMessage &message);
[[nodiscard]] MessageEvent DeletedMessageSnapshot(
	not_null<Main::Session*> session,
	HistoryItem *item,
	MsgId id,
	PeerId peer = PeerId());

}
