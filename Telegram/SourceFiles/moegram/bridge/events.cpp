#include "moegram/bridge/events.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"

namespace MoeGram::Bridge {

Plugins::NativeEventScope ObserveUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	return Plugins::NativeEventScope(Plugins::ObserveTelegramUpdate(session, update));
}

void PublishSentMessage(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	auto event = Plugins::MessageEvent();
	event.accountId = session->uniqueId();
	event.messageId = item->id.bare;
	event.chatId = Plugins::DialogId(item->history()->peer->id);
	if (item->hasRealFromId()) {
		event.senderId = Plugins::DialogId(item->from()->id);
	}
	event.text = item->originalText().text;
	event.outgoing = item->out();
	Plugins::Publish(std::move(event));
}

Plugins::UpdateEvent PrepareDeletedMessages(
		not_null<Main::Session*> session,
		std::optional<PeerId> peer,
		const QVector<MTPint> &ids) {
	auto event = Plugins::UpdateEvent();
	if (!Plugins::ModelEventsEnabled()) {
		return event;
	}
	event.accountId = session->uniqueId();
	event.name = peer && peerIsChannel(*peer)
		? u"TL_updateDeleteChannelMessages"_q : u"TL_updateDeleteMessages"_q;
	for (const auto &id : ids) {
		const auto item = peer
			? session->data().message(*peer, id.v)
			: session->data().nonChannelMessage(id.v);
		event.messages.push_back(Plugins::DeletedMessageSnapshot(
			session, item, id.v, peer.value_or(PeerId())));
	}
	return event;
}

void PublishDeletedMessages(Plugins::UpdateEvent event, int pts, int ptsCount) {
	if (!event.messages.empty()) {
		event.pts = pts;
		event.ptsCount = ptsCount;
		Plugins::Publish(std::move(event));
	}
}

void ObserveDeletedMessages(
		not_null<Main::Session*> session,
		std::optional<PeerId> peer,
		const QVector<MTPint> &ids) {
	if (Plugins::ModelEventsEnabled()) {
		Plugins::Publish(PrepareDeletedMessages(session, peer, ids));
	}
}

}
