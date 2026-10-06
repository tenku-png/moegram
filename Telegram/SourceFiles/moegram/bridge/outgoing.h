#pragma once

#include "base/basic_types.h"
#include "plugins/plugin_host.h"

namespace Main { class Session; }
namespace Api { struct MessageToSend; }

namespace MoeGram::Bridge {

using OutgoingValidity = std::function<bool()>;
using OutgoingTextSend = std::function<void(QString)>;
using OutgoingMessageSend = std::function<void(
	not_null<Main::Session*>, Api::MessageToSend&&)>;

void FilterOutgoingMessage(
	not_null<Main::Session*> session,
	Api::MessageToSend &&message,
	OutgoingMessageSend send);
void FilterOutgoingText(
	quint64 accountId,
	QString original,
	OutgoingValidity valid,
	OutgoingTextSend send);

}
