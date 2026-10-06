#include "moegram/bridge/outgoing.h"

namespace MoeGram::Bridge {

void FilterOutgoingText(
		quint64 accountId,
		QString original,
		OutgoingValidity valid,
		OutgoingTextSend send) {
	Plugins::FilterOutgoing(accountId, std::move(original),
		[valid = std::move(valid), send = std::move(send)](
				std::optional<QString> filtered) {
			if (filtered && valid()) {
				send(std::move(*filtered));
			}
		});
}

}
