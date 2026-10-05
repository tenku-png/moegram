#include "plugins/telegram_event_adapter.h"

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"

namespace MoeGram::Plugins {

qint64 DialogId(PeerId peer) {
	if (peerIsChannel(peer)) {
		return -1000000000000LL - qint64(peerToChannel(peer).bare);
	} else if (peerIsChat(peer)) {
		return -qint64(peerToChat(peer).bare);
	}
	return qint64(peerToUser(peer).bare);
}

bool ObserveTelegramUpdate(
		not_null<Main::Session*> session,
		const MTPUpdate &update) {
	auto event = UpdateEvent();
	event.accountId = session->uniqueId();
	event.native = true;
	const auto addMessage = [&](const auto &data, EventKind kind, QString name) {
		event.name = std::move(name);
		event.pts = data.vpts().v;
		event.ptsCount = data.vpts_count().v;
		if (data.vmessage().type() != mtpc_message) {
			return;
		}
		const auto &message = data.vmessage().c_message();
		const auto peer = peerFromMTP(message.vpeer_id());
		auto snapshot = MessageEvent();
		snapshot.kind = kind;
		snapshot.accountId = event.accountId;
		snapshot.messageId = message.vid().v;
		snapshot.chatId = DialogId(peer);
		if (const auto sender = message.vfrom_id()) {
			snapshot.senderId = DialogId(peerFromMTP(*sender));
		} else if (peerIsUser(peer)) {
			snapshot.senderId = DialogId(message.is_out()
				? session->userPeerId() : peer);
		}
		snapshot.text = qs(message.vmessage());
		snapshot.outgoing = message.is_out();
		event.messages.push_back(std::move(snapshot));
	};
	const auto addDelete = [&](const auto &data, PeerId peer, QString name) {
		event.name = std::move(name);
		event.pts = data.vpts().v;
		event.ptsCount = data.vpts_count().v;
		for (const auto &id : data.vmessages().v) {
			auto snapshot = MessageEvent();
			snapshot.kind = EventKind::Delete;
			snapshot.accountId = event.accountId;
			snapshot.messageId = id.v;
			const auto item = peer
				? session->data().message(peer, id.v)
				: session->data().nonChannelMessage(id.v);
			if (item) {
				snapshot.chatId = DialogId(item->history()->peer->id);
				if (item->hasRealFromId()) {
					snapshot.senderId = DialogId(item->from()->id);
				}
				snapshot.text = item->originalText().text;
				snapshot.outgoing = item->out();
			} else if (peer) {
				snapshot.chatId = DialogId(peer);
			}
			event.messages.push_back(std::move(snapshot));
		}
	};
	switch (update.type()) {
	case mtpc_updateNewMessage:
		addMessage(update.c_updateNewMessage(), EventKind::Message, u"TL_updateNewMessage"_q);
		break;
	case mtpc_updateNewChannelMessage:
		addMessage(update.c_updateNewChannelMessage(), EventKind::Message, u"TL_updateNewChannelMessage"_q);
		break;
	case mtpc_updateEditMessage:
		addMessage(update.c_updateEditMessage(), EventKind::Edit, u"TL_updateEditMessage"_q);
		break;
	case mtpc_updateEditChannelMessage:
		addMessage(update.c_updateEditChannelMessage(), EventKind::Edit, u"TL_updateEditChannelMessage"_q);
		break;
	case mtpc_updateDeleteMessages:
		addDelete(update.c_updateDeleteMessages(), PeerId(), u"TL_updateDeleteMessages"_q);
		break;
	case mtpc_updateDeleteChannelMessages: {
		const auto &data = update.c_updateDeleteChannelMessages();
		addDelete(data, peerFromChannel(data.vchannel_id()), u"TL_updateDeleteChannelMessages"_q);
	} break;
	default:
		return false;
	}
	if (!event.messages.empty()) {
		Publish(std::move(event));
	}
	return true;
}

void PublishModelMessage(
		not_null<Main::Session*> session,
		MoeGram::Plugins::EventKind kind,
		const MTPMessage &message) {
	if (!MoeGram::Plugins::ModelEventsEnabled()
		|| message.type() != mtpc_message) {
		return;
	}
	const auto &data = message.c_message();
	const auto peer = peerFromMTP(data.vpeer_id());
	auto event = MoeGram::Plugins::MessageEvent();
	event.kind = kind;
	event.accountId = session->uniqueId();
	event.messageId = data.vid().v;
	event.chatId = DialogId(peer);
	if (const auto sender = data.vfrom_id()) {
		event.senderId = DialogId(peerFromMTP(*sender));
	} else if (peerIsUser(peer)) {
		event.senderId = DialogId(data.is_out()
			? session->userPeerId() : peer);
	}
	event.text = qs(data.vmessage());
	event.outgoing = data.is_out();
	MoeGram::Plugins::Publish(std::move(event));
}

MoeGram::Plugins::MessageEvent DeletedMessageSnapshot(
		not_null<Main::Session*> session,
		HistoryItem *item,
		MsgId id,
		PeerId peer) {
	auto event = MoeGram::Plugins::MessageEvent();
	event.kind = MoeGram::Plugins::EventKind::Delete;
	event.accountId = session->uniqueId();
	event.messageId = id.bare;
	if (item) {
		event.chatId = DialogId(item->history()->peer->id);
		if (item->hasRealFromId()) {
			event.senderId = DialogId(item->from()->id);
		}
		event.text = item->originalText().text;
		event.outgoing = item->out();
	} else if (peer) {
		event.chatId = DialogId(peer);
	}
	return event;
}

MessageEvent MessageSnapshot(
		not_null<Main::Session*> session,
		not_null<HistoryItem*> item) {
	auto event = MessageEvent();
	event.kind = EventKind::Message;
	event.accountId = session->uniqueId();
	event.messageId = item->id.bare;
	event.chatId = DialogId(item->history()->peer->id);
	if (item->hasRealFromId()) {
		event.senderId = DialogId(item->from()->id);
	} else if (peerIsUser(item->history()->peer->id)) {
		event.senderId = DialogId(item->out()
			? session->userPeerId()
			: item->history()->peer->id);
	}
	event.text = item->originalText().text;
	event.outgoing = item->out();
	return event;
}

}
