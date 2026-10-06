#pragma once

#include "plugins/telegram_event_adapter.h"

namespace MoeGram::Bridge {

[[nodiscard]] Plugins::NativeEventScope ObserveUpdate(
	not_null<Main::Session*> session,
	const MTPUpdate &update);
void PublishSentMessage(not_null<Main::Session*> session, not_null<HistoryItem*> item);
[[nodiscard]] Plugins::UpdateEvent PrepareDeletedMessages(
	not_null<Main::Session*> session,
	std::optional<PeerId> peer,
	const QVector<MTPint> &ids);
void PublishDeletedMessages(Plugins::UpdateEvent event, int pts, int ptsCount);
void ObserveDeletedMessages(
	not_null<Main::Session*> session,
	std::optional<PeerId> peer,
	const QVector<MTPint> &ids);

}
