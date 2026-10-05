#include "plugins/telegram_request_adapter.h"

#include "scheme.h"

namespace MoeGram::Plugins {

std::optional<RequestEvent> RequestSnapshot(
		quint64 accountId, const MTP::details::SerializedRequest &request) {
	if (!request || request->size() <= MTP::details::SerializedRequest::kMessageBodyPosition) {
		return std::nullopt;
	}
	auto from = request->constData() + MTP::details::SerializedRequest::kMessageBodyPosition;
	const auto end = request->constData() + request->size();
	if (*from == mtpc_messages_setTyping) {
		auto parsed = MTPmessages_SetTyping();
		if (parsed.read(from, end)) {
			return RequestEvent{ u"TL_messages_setTyping"_q, accountId, std::nullopt };
		}
	} else if (*from == mtpc_account_updateStatus) {
		auto parsed = MTPaccount_UpdateStatus();
		if (parsed.read(from, end)) {
			return RequestEvent{ u"TL_account_updateStatus"_q, accountId,
				(*request)[MTP::details::SerializedRequest::kMessageBodyPosition + 1] == mtpc_boolTrue };
		}
	}
	return std::nullopt;
}

void ApplyRequestSnapshot(
		MTP::details::SerializedRequest &request, const RequestEvent &event) {
	if (event.name == u"TL_account_updateStatus"_q && event.offline) {
		const auto position = MTP::details::SerializedRequest::kMessageBodyPosition;
		Expects(request && request->size() > position + 1);
		Expects((*request)[position] == mtpc_account_updateStatus);
		(*request)[position + 1] = *event.offline ? mtpc_boolTrue : mtpc_boolFalse;
	}
}

void ObserveRequestResponse(const RequestEvent &event, const MTP::Response &response) {
	auto from = response.reply.constData();
	auto parsed = MTPBool();
	if (parsed.read(from, from + response.reply.size())) {
		Publish(RequestResultEvent{ event.name, event.accountId,
			mtpIsTrue(parsed), std::nullopt, {} });
	}
}

void ObserveRequestError(const RequestEvent &event, const MTP::Error &error) {
	Publish(RequestResultEvent{ event.name, event.accountId, std::nullopt,
		error.code(), error.type() + u": "_q + error.description() });
}

}
