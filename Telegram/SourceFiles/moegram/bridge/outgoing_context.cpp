#include "moegram/bridge/outgoing.h"

#include "api/api_common.h"
#include "history/history.h"
#include "main/main_session.h"
#include "plugins/telegram_event_adapter.h"

namespace MoeGram::Bridge {

void FilterOutgoingMessage(
		not_null<Main::Session*> session,
		Api::MessageToSend &&message,
		OutgoingMessageSend send) {
	const auto original = message.textWithTags.text;
	auto outgoing = Plugins::OutgoingMessage{
		.accountId = session->uniqueId(),
		.text = original,
		.chatId = Plugins::DialogId(message.action.history->peer->id),
	};
	if (const auto reply = message.action.replyTo.messageId) {
		outgoing.replyChatId = Plugins::DialogId(reply.peer);
		outgoing.replyMessageId = qint64(reply.msg.bare);
	}
	const auto pending = std::make_shared<Api::MessageToSend>(std::move(message));
	const auto weak = base::make_weak(session);
	Plugins::FilterOutgoing(std::move(outgoing),
		[weak, pending, original, send = std::move(send)](
				std::optional<QString> filtered) {
			const auto session = weak.get();
			if (!filtered || !session) {
				return;
			}
			if (*filtered != original) {
				pending->textWithTags.text = std::move(*filtered);
				pending->textWithTags.tags.clear();
				pending->webPage = {};
			}
			send(session, std::move(*pending));
		});
}

}
