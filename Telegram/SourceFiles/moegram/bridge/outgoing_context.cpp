#include "moegram/bridge/outgoing.h"

#include "api/api_common.h"
#include "main/main_session.h"

namespace MoeGram::Bridge {

void FilterOutgoingMessage(
		not_null<Main::Session*> session,
		Api::MessageToSend &&message,
		OutgoingMessageSend send) {
	const auto original = message.textWithTags.text;
	const auto pending = std::make_shared<Api::MessageToSend>(std::move(message));
	const auto weak = base::make_weak(session);
	FilterOutgoingText(session->uniqueId(), original,
		[weak] { return weak.get() != nullptr; },
		[weak, pending, original, send = std::move(send)](QString filtered) {
			if (const auto session = weak.get()) {
				if (filtered != original) {
					pending->textWithTags.text = std::move(filtered);
					pending->textWithTags.tags.clear();
					pending->webPage = {};
				}
				send(session, std::move(*pending));
			}
		});
}

}
