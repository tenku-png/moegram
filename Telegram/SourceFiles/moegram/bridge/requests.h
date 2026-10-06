#pragma once

#include "plugins/telegram_request_adapter.h"

namespace MTP { class Instance; }

namespace MoeGram::Bridge {

using RequestValidity = std::function<bool()>;
using RequestSend = std::function<void(const MTP::details::SerializedRequest&)>;
using RequestCancel = std::function<void(const MTP::Response&)>;

[[nodiscard]] std::optional<Plugins::RequestEvent> PrepareRequestHooks(
	MTP::Instance *instance,
	MTP::details::SerializedRequest &request,
	MTP::ResponseHandler &callbacks);
[[nodiscard]] std::optional<Plugins::RequestEvent> PrepareRequestHooks(
	std::optional<Plugins::RequestEvent> snapshot,
	MTP::details::SerializedRequest &request,
	MTP::ResponseHandler &callbacks);
void DispatchRequest(
	Plugins::RequestEvent snapshot,
	MTP::details::SerializedRequest request,
	RequestValidity valid,
	RequestSend send,
	RequestCancel cancel);

}
